#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "pin_config.h"
#include "audio_fft.h"
#include "lcd_st7796.h"
#include "spectrum_ui.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "=== Music Spectrum ===");
    ESP_LOGI(TAG, "ESP32-S3 | ST7796 320x320 | INMP411 PDM");
    ESP_LOGI(TAG, "FFT:%d SR:%dHz Bars:%d", FFT_SIZE, SAMPLE_RATE, DISPLAY_BARS);

    // 1. 外设初始化
    audio_fft_init();
    lcd_lvgl_init();

    // 2. LVGL 显示驱动
    static lv_disp_drv_t disp_drv;
    lvgl_set_display_driver(&disp_drv);
    lv_disp_drv_register(&disp_drv);

    // 3. LVGL Tick
    lvgl_tick_init();

    // 4. 频谱 UI
    spectrum_ui_init();

    // 5. 任务创建
    xTaskCreatePinnedToCore(audio_fft_task, "audio_fft",
                            AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL, AUDIO_TASK_CORE);
    xTaskCreatePinnedToCore(spectrum_update_task, "ui_update",
                            UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL, UI_TASK_CORE);
    xTaskCreatePinnedToCore(display_task, "lvgl",
                            DISPLAY_TASK_STACK, NULL, DISPLAY_TASK_PRIO, NULL, DISPLAY_TASK_CORE);

    ESP_LOGI(TAG, "Core0: audio_fft+ui  Core1: lvgl");
}
