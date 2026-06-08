#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esp_log.h"
#include "pin_config.h"
#include "audio_fft.h"

static const char *TAG = "ui";

// ---- 全屏布局 ----
#define BAR_GAP       3
#define BAR_WIDTH     ((LCD_WIDTH - (DISPLAY_BARS + 1) * BAR_GAP) / DISPLAY_BARS)
#define BAR_MAX_H     (LCD_HEIGHT - 20)
#define BASELINE_Y    (LCD_HEIGHT - 6)

// ---- 显示阻尼 ----
static float display_val[DISPLAY_BARS] = {0};
#define DISPLAY_SMOOTH  0.35f

// ---- 峰值保持 ----
static float ui_peak[DISPLAY_BARS]     = {0};
static int   ui_peak_age[DISPLAY_BARS] = {0};
#define UI_PEAK_FRAMES  40

// ---- 彩虹渐变 (青→绿→黄→红→紫) ----
static lv_color_t bar_color(int bar, float intensity) {
    int r, g, b;
    float t = (float)bar / (DISPLAY_BARS - 1);

    // 5阶渐变: 青(0)→绿(0.15)→黄(0.4)→橙(0.65)→红(0.85)→紫(1.0)
    if (t < 0.15f) {
        float s = t / 0.15f;
        r = 0;   g = 220 + (int)(s * 35);  b = 220 - (int)(s * 220);
    } else if (t < 0.40f) {
        float s = (t - 0.15f) / 0.25f;
        r = (int)(s * 255);  g = 255;  b = 0;
    } else if (t < 0.65f) {
        float s = (t - 0.40f) / 0.25f;
        r = 255;  g = 255 - (int)(s * 200);  b = 0;
    } else if (t < 0.85f) {
        float s = (t - 0.65f) / 0.20f;
        r = 255;  g = 55 - (int)(s * 55);  b = 0;
    } else {
        float s = (t - 0.85f) / 0.15f;
        r = 255 - (int)(s * 80);  g = 0;  b = (int)(s * 180);
    }

    // 强度衰减：柱子矮时颜色暗淡一些
    float dim = 0.6f + 0.4f * intensity;
    r = (int)(r * dim);
    g = (int)(g * dim);
    b = (int)(b * dim);

    return lv_color_make(r > 255 ? 255 : r,
                          g > 255 ? 255 : g,
                          b > 255 ? 255 : b);
}

// ---- 背景绘制 (暗色渐变 + 参考线) ----
static void draw_background(lv_draw_ctx_t *draw_ctx) {
    // 半透明暗色底
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_black();
    bg.bg_opa  = LV_OPA_COVER;
    lv_area_t bg_area = {0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1};
    lv_draw_rect(draw_ctx, &bg, &bg_area);

    // 参考刻度线 (1/4, 2/4, 3/4 高度处)
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0x111111);
    line.width = 1;
    line.opa   = LV_OPA_40;

    for (int q = 1; q <= 3; q++) {
        int y = BASELINE_Y - (BAR_MAX_H * q / 4);
        lv_point_t p1 = {0, y};
        lv_point_t p2 = {LCD_WIDTH - 1, y};
        lv_draw_line(draw_ctx, &line, &p1, &p2);
    }
}

// ---- 自定义绘制 ----
static void spectrum_draw_cb(lv_event_t *e) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    draw_background(draw_ctx);

    lv_draw_rect_dsc_t rect;
    lv_draw_line_dsc_t  line;
    int half_gap = BAR_GAP / 2;

    for (int i = 0; i < DISPLAY_BARS; i++) {
        float val = display_val[i];
        int h = (int)(val * BAR_MAX_H);
        if (h < 2) h = 2;
        if (h > BAR_MAX_H) h = BAR_MAX_H;

        int x = BAR_GAP + i * (BAR_WIDTH + BAR_GAP);
        int y = BASELINE_Y - h;

        // ---- 先擦除整列区域，防止柱子变矮时旧白线/峰值点残影 ----
        lv_draw_rect_dsc_init(&rect);
        rect.bg_color = lv_color_black();
        rect.bg_opa  = LV_OPA_COVER;
        lv_area_t erase = {x + half_gap, BASELINE_Y - BAR_MAX_H,
                           x + BAR_WIDTH - half_gap - 1, BASELINE_Y};
        lv_draw_rect(draw_ctx, &rect, &erase);

        // ---- 柱子主体 (上下渐变) ----
        lv_color_t top_color = bar_color(i, val);
        // 底部颜色更深 (dim 0.4)
        int rb = top_color.ch.red   < 40 ? 40 : (int)(top_color.ch.red   * 0.4f);
        int gb = top_color.ch.green < 20 ? 20 : (int)(top_color.ch.green * 0.4f);
        int bb = top_color.ch.blue  < 10 ? 10 : (int)(top_color.ch.blue  * 0.4f);

        // 用实心矩形 + 顶部亮线 模拟渐变
        lv_draw_rect_dsc_init(&rect);
        rect.bg_color = lv_color_make(top_color.ch.red,
                                       top_color.ch.green,
                                       top_color.ch.blue);
        rect.bg_opa  = LV_OPA_COVER;
        rect.radius  = 2;
        rect.border_color = lv_color_hex(0x000000);
        rect.border_opa   = LV_OPA_20;
        rect.border_width = 1;

        lv_area_t bar_area = {x + half_gap, y, x + BAR_WIDTH - half_gap - 1, BASELINE_Y};
        lv_draw_rect(draw_ctx, &rect, &bar_area);

        // 顶部高亮边
        if (h > 6) {
            lv_draw_rect_dsc_init(&rect);
            rect.bg_color = lv_color_white();
            rect.bg_opa  = LV_OPA_40;
            rect.radius  = 2;
            lv_area_t top = {x + half_gap, y, x + BAR_WIDTH - half_gap - 1, y + 3};
            lv_draw_rect(draw_ctx, &rect, &top);
        }

        // ---- 柱内发光中线 ----
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_white();
        line.width = 1;
        line.opa   = LV_OPA_20;
        int cx = x + BAR_WIDTH / 2;
        lv_point_t lp1 = {cx, y + 4};
        lv_point_t lp2 = {cx, BASELINE_Y - 2};
        lv_draw_line(draw_ctx, &line, &lp1, &lp2);
    }

    // ---- 峰值保持点 ----
    for (int i = 0; i < DISPLAY_BARS; i++) {
        if (ui_peak[i] < 0.02f) continue;
        int ph = (int)(ui_peak[i] * BAR_MAX_H);
        if (ph < 4) ph = 4;
        int px = BAR_GAP + i * (BAR_WIDTH + BAR_GAP) + BAR_WIDTH / 2;
        int py = BASELINE_Y - ph;

        // 外发光
        lv_draw_rect_dsc_t glow;
        lv_draw_rect_dsc_init(&glow);
        glow.bg_color = bar_color(i, ui_peak[i]);
        glow.bg_opa  = LV_OPA_40;
        glow.radius  = LV_RADIUS_CIRCLE;
        lv_area_t ga = {px - 5, py - 5, px + 5, py + 5};
        lv_draw_rect(draw_ctx, &glow, &ga);

        // 白点核心
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = lv_color_white();
        dot.bg_opa  = (ui_peak_age[i] < UI_PEAK_FRAMES / 3) ? LV_OPA_COVER : LV_OPA_60;
        dot.radius  = LV_RADIUS_CIRCLE;
        lv_area_t da = {px - 2, py - 2, px + 2, py + 2};
        lv_draw_rect(draw_ctx, &dot, &da);
    }
}

// ---- 频谱更新 Task ----
void spectrum_update_task(void *arg) {
    while (1) {
        if (xSemaphoreTake(spectrum_ready_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        for (int i = 0; i < DISPLAY_BARS; i++) {
            float raw = spectrum_display[i];

            display_val[i] = DISPLAY_SMOOTH * raw +
                             (1.0f - DISPLAY_SMOOTH) * display_val[i];

            if (display_val[i] > ui_peak[i]) {
                ui_peak[i] = display_val[i];
                ui_peak_age[i] = 0;
            } else {
                if (++ui_peak_age[i] > UI_PEAK_FRAMES) {
                    ui_peak[i] *= 0.92f;
                }
            }
        }

        lv_obj_invalidate(lv_scr_act());
    }
}

// ---- 初始化 ----
void spectrum_ui_init(void) {
    ESP_LOGI(TAG, "Fullscreen spectrum: %d bars, bar_w=%d gap=%d max_h=%d",
             DISPLAY_BARS, BAR_WIDTH, BAR_GAP, BAR_MAX_H);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, spectrum_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    ESP_LOGI(TAG, "Ready");
}
