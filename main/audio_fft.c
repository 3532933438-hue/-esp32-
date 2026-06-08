#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_dsp.h"
#include "esp_log.h"
#include "pin_config.h"

static const char *TAG = "audio_fft";

// ---- I2S ----
static i2s_chan_handle_t rx_chan;
static int16_t *pcm_buf;

// ---- FFT 工作区 (PSRAM) ----
static float *window_coeff;
static float *fft_real;
static float *fft_complex;
static float  magnitude[FFT_SIZE/2];

// ---- 频谱输出 ----
float spectrum_display[DISPLAY_BARS];
SemaphoreHandle_t spectrum_ready_sem;

// ---- 对数频段 ----
static const int band_start[DISPLAY_BARS] = {
    0,  1,  2,  3,  4,  5,  6,  7,
    9, 11, 13, 15, 17, 20, 23, 26,
    30, 34, 38, 43, 48, 54, 61, 69,
    78, 88, 100, 114, 130, 150, 180, 230
};
static const int band_end[DISPLAY_BARS] = {
    1,  2,  3,  4,  5,  6,  7,  9,
    11, 13, 15, 17, 20, 23, 26, 30,
    34, 38, 43, 48, 54, 61, 69, 78,
    88, 100, 114, 130, 150, 180, 230, 320
};

// ---- 指数平滑 + 峰值保持 ----
static float smooth_val[DISPLAY_BARS] = {0};
static float peak_val[DISPLAY_BARS] = {0};
static int   peak_timer[DISPLAY_BARS] = {0};
#define SMOOTH_UP      0.4f
#define SMOOTH_DOWN    0.15f
#define PEAK_HOLD_MS   800
#define PEAK_FRAMES    (PEAK_HOLD_MS / (FFT_SIZE * 1000 / SAMPLE_RATE))

// ---- 初始化 ----
void audio_fft_init(void) {
    ESP_LOGI(TAG, "Init I2S std: SCK=%d WS=%d SD=%d, %dHz",
             PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DATA, SAMPLE_RATE);

    // ==== I2S 标准模式 (Philips) ====
    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = FFT_SIZE,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .din  = PIN_I2S_DATA,
            .dout = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // ==== FFT 内存 (PSRAM) ====
    pcm_buf      = heap_caps_calloc(FFT_SIZE,     sizeof(int16_t), MALLOC_CAP_SPIRAM);
    window_coeff = heap_caps_calloc(FFT_SIZE,     sizeof(float),   MALLOC_CAP_SPIRAM);
    fft_real     = heap_caps_calloc(FFT_SIZE,     sizeof(float),   MALLOC_CAP_SPIRAM);
    fft_complex  = heap_caps_calloc(FFT_SIZE * 2, sizeof(float),   MALLOC_CAP_SPIRAM);

    // ==== Hann 窗 + FFT ====
    dsps_wind_hann_f32(window_coeff, FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, FFT_SIZE);

    spectrum_ready_sem = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "Ready");
}

// ---- Audio + FFT Task (Core 0) ----
void audio_fft_task(void *arg) {
    size_t bytes_read;

    while (1) {
        i2s_channel_read(rx_chan, pcm_buf, FFT_SIZE * sizeof(int16_t),
                         &bytes_read, portMAX_DELAY);

        // int16 PCM → float + Hann 窗
        for (int i = 0; i < FFT_SIZE; i++) {
            fft_real[i] = (float)pcm_buf[i] / 32768.0f * window_coeff[i];
        }

        // 实→复 FFT
        for (int i = 0; i < FFT_SIZE; i++) {
            fft_complex[i * 2]     = fft_real[i];
            fft_complex[i * 2 + 1] = 0.0f;
        }
        dsps_bit_rev_fc32(fft_complex, FFT_SIZE);
        dsps_cplx2reC_fc32(fft_complex, FFT_SIZE);

        // 幅度谱
        for (int i = 0; i < FFT_SIZE / 2; i++) {
            float re = fft_complex[i * 2];
            float im = fft_complex[i * 2 + 1];
            magnitude[i] = sqrtf(re * re + im * im) / 256.0f;
        }

        // 频段合并 + 平滑 + 峰值
        for (int b = 0; b < DISPLAY_BARS; b++) {
            float sum = 0;
            int count = band_end[b] - band_start[b];
            for (int j = band_start[b]; j < band_end[b]; j++) {
                sum += magnitude[j];
            }
            float val = sum / (float)count;

            float alpha = (val > smooth_val[b]) ? SMOOTH_UP : SMOOTH_DOWN;
            smooth_val[b] = alpha * val + (1.0f - alpha) * smooth_val[b];

            if (smooth_val[b] > peak_val[b]) {
                peak_val[b] = smooth_val[b];
                peak_timer[b] = 0;
            } else if (++peak_timer[b] > PEAK_FRAMES) {
                peak_val[b] *= 0.95f;
            }

            spectrum_display[b] = smooth_val[b];
        }

        xSemaphoreGive(spectrum_ready_sem);
    }
}
