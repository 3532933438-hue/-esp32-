#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "pin_config.h"

static const char *TAG = "lcd";
static spi_device_handle_t spi;

// LVGL 双缓冲
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1, *buf2;

// ---- 手动 CS + DC, 和 Arduino 测试完全一致 ----
static inline void cs_low()  { gpio_set_level(PIN_LCD_CS, 0); }
static inline void cs_high() { gpio_set_level(PIN_LCD_CS, 1); }
static inline void dc_cmd()  { gpio_set_level(PIN_LCD_DC, 0); }
static inline void dc_data() { gpio_set_level(PIN_LCD_DC, 1); }

static void spi_write8(uint8_t d) {
    spi_transaction_t t = { .length = 8, .tx_data = {d}, .flags = SPI_TRANS_USE_TXDATA };
    cs_low(); spi_device_transmit(spi, &t); cs_high();
}

static void spi_write16(uint16_t d) {
    spi_transaction_t t = { .length = 16, .tx_data = {(uint8_t)(d>>8), (uint8_t)d}, .flags = SPI_TRANS_USE_TXDATA };
    cs_low(); spi_device_transmit(spi, &t); cs_high();
}

static void cmd(uint8_t c)  { dc_cmd();  spi_write8(c); }
static void dat(uint8_t d)  { dc_data(); spi_write8(d); }
static void dat16(uint16_t d) { dc_data(); spi_write16(d); }
static void datN(const uint8_t *d, int n) { for (int i = 0; i < n; i++) dat(d[i]); }

// ---- 设置窗口 ----
static void set_window(int x1, int y1, int x2, int y2) {
    cmd(0x2A); dat16(x1); dat16(x2);
    cmd(0x2B); dat16(y1); dat16(y2);
    cmd(0x2C);
}

// ---- 全屏刷色自检 ----
// 全屏填充 — 用 uint8_t 缓冲避免字节序问题
static void fill_screen(uint16_t color) {
    uint8_t hi = color >> 8, lo = color & 0xFF;
    uint8_t *buf = heap_caps_malloc(320 * 320 * 2, MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGE(TAG, "MALLOC FAIL"); return; }
    for (int i = 0; i < 320 * 320; i++) {
        buf[i*2]   = hi;
        buf[i*2+1] = lo;
    }
    set_window(0, 0, 319, 319);
    dc_data(); cs_low();
    spi_transaction_t dt = { .length = 320 * 320 * 16, .tx_buffer = buf };
    spi_device_transmit(spi, &dt);
    cs_high();
    heap_caps_free(buf);
    ESP_LOGI(TAG, "fill_screen done");
}

// ---- 面板复位 ----
static void panel_reset(void) {
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));
}

// 5 种初始化，和 Arduino 测试完全一致
static void init_type1(void) {
    ESP_LOGI(TAG, "Type1: ST7789-like, MADCTL=0x00, inversion");
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x3A); dat(0x55);
    cmd(0x36); dat(0x00);
    cmd(0x21);
    cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}
static void init_type2(void) {
    ESP_LOGI(TAG, "Type2: No inversion");
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x3A); dat(0x55);
    cmd(0x36); dat(0x00);
    cmd(0x13);
    cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}
static void init_type3(void) {
    ESP_LOGI(TAG, "Type3: MADCTL=0x08 (BGR)");
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x3A); dat(0x55);
    cmd(0x36); dat(0x08);
    cmd(0x21);
    cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}
static void init_type4(void) {
    ESP_LOGI(TAG, "Type4: Full ST7796, gamma+inversion");
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0xF0); dat(0xC3);
    cmd(0xF0); dat(0x96);
    cmd(0xB0); dat(0x10);
    cmd(0xB3); dat(0x07);
    cmd(0xB4); dat(0x00);
    { uint8_t g[] = {0xF0,0x09,0x0B,0x07,0x07,0x2F,0x34,0x42,0x43,0x38,0x16,0x17,0x2A,0x2D}; cmd(0xE0); datN(g,14); }
    { uint8_t g[] = {0xF0,0x09,0x0B,0x07,0x05,0x2D,0x34,0x42,0x42,0x38,0x15,0x16,0x2A,0x2D}; cmd(0xE1); datN(g,14); }
    cmd(0x3A); dat(0x55);
    cmd(0x36); dat(0x00);
    cmd(0x21);
    cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}
static void init_type5(void) {
    ESP_LOGI(TAG, "Type5: Industrial panel, MADCTL=0x48");
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0xF0); dat(0xC3);
    cmd(0xF0); dat(0x96);
    cmd(0x36); dat(0x48);
    cmd(0x3A); dat(0x55);
    cmd(0xB0); dat(0x00);
    cmd(0xB1); dat(0xA0);
    cmd(0xB4); dat(0x02);
    cmd(0xB5); datN((uint8_t[]){0x00,0x00}, 2);
    cmd(0xB6); datN((uint8_t[]){0x82,0x3E,0x00}, 3);
    cmd(0xB7); dat(0x06);
    cmd(0xC0); datN((uint8_t[]){0x10,0x10}, 2);
    cmd(0xC1); dat(0x41);
    cmd(0xC2); datN((uint8_t[]){0x22,0x08}, 2);
    cmd(0xC5); datN((uint8_t[]){0x00,0x22,0x80}, 3);
    { uint8_t g[] = {0xF0,0x09,0x0B,0x07,0x07,0x2F,0x34,0x42,0x43,0x38,0x16,0x17,0x2A,0x2D}; cmd(0xE0); datN(g,14); }
    { uint8_t g[] = {0xF0,0x09,0x0B,0x07,0x05,0x2D,0x34,0x42,0x42,0x38,0x15,0x16,0x2A,0x2D}; cmd(0xE1); datN(g,14); }
    cmd(0x35); dat(0x00);
    cmd(0x21);
    cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}

typedef void (*init_fn)(void);
static init_fn init_funcs[] = {init_type1, init_type2, init_type3, init_type4, init_type5};

static void st7796_init(void) {
    for (int t = 0; t < 5; t++) {
        panel_reset();
        init_funcs[t]();
        fill_screen(0xF800);  // 全红
        vTaskDelay(pdMS_TO_TICKS(600));
        fill_screen(0x07E0);  // 全绿
        vTaskDelay(pdMS_TO_TICKS(600));
        fill_screen(0x001F);  // 全蓝
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    set_window(0, 0, 319, 319);
    ESP_LOGI(TAG, "All 5 init types done - check which showed colors");
}

// ---- LVGL flush ----
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    if (w <= 0 || h <= 0) { lv_disp_flush_ready(drv); return; }

    set_window(area->x1, area->y1, area->x2, area->y2);

    dc_data(); cs_low();
    spi_transaction_t t = { .length = w * h * 16, .tx_buffer = color_map };
    spi_device_transmit(spi, &t);
    cs_high();

    lv_disp_flush_ready(drv);
}

// ---- 初始化入口 ----
void lcd_lvgl_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LCD_RST) | (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_CS) | (1ULL << PIN_LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    cs_high();
    gpio_set_level(PIN_LCD_BL, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 320 * 2,  // 全屏 DMA
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,  // 和 Arduino 测试同步
        .spics_io_num = -1,                   // 手动 CS
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &spi));

    st7796_init();

    lv_init();
    size_t sz = LCD_WIDTH * LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    buf2 = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_WIDTH * LVGL_BUF_HEIGHT);
    ESP_LOGI(TAG, "LVGL dual buf: 320x%d x2", LVGL_BUF_HEIGHT);
}

void lvgl_set_display_driver(lv_disp_drv_t *drv) {
    lv_disp_drv_init(drv);
    drv->hor_res = LCD_WIDTH;
    drv->ver_res = LCD_HEIGHT;
    drv->flush_cb = lvgl_flush_cb;
    drv->draw_buf = &draw_buf;
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(5); }

void lvgl_tick_init(void) {
    static esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = lvgl_tick_cb, .name = "lvgl" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, 5000));
}

void display_task(void *arg) {
    while (1) { lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(5)); }
}
