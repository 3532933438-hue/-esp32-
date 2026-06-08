#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH      16
#define LV_COLOR_16_SWAP    1       // ESP SPI 字节序交换
#define LV_USE_PERF_MONITOR 0
#define LV_USE_LOG          0

// ---- 内存 ----
#define LV_MEM_SIZE         (64 * 1024)
#define LV_MEM_CUSTOM       0

// ---- 显示缓冲 ----
#define LV_DISP_DEF_REFR_PERIOD  16   // ~60Hz
#define LV_INDEV_DEF_READ_PERIOD 30

// ---- GPU 加速 ----
#define LV_USE_GPU_ARM2D    0

// ---- 功能裁剪 ----
#define LV_USE_ANIMATION    1
#define LV_USE_SHADOW       0
#define LV_USE_BLEND_MODES  0
#define LV_USE_GRADIENT     1

// ---- 字体 ----
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

// ---- 控件 ----
#define LV_USE_BAR           1
#define LV_USE_LABEL         1
#define LV_USE_CHART         0

// ---- 调试 ----
#define LV_USE_DEBUG         0

#endif
