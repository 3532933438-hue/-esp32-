#pragma once

// ============================================================
// ESP32-S3 N16R8 引脚分配
// ============================================================

// ---- ST7796 4线SPI (320x320) ----
#define PIN_LCD_SCLK   12
#define PIN_LCD_MOSI   11
#define PIN_LCD_DC     10
#define PIN_LCD_CS     9
#define PIN_LCD_RST    13
#define PIN_LCD_BL     14
#define LCD_SPI_HOST   SPI2_HOST

// ---- INMP411 I2S 数字麦克风 ----
#define PIN_I2S_BCLK   41
#define PIN_I2S_WS     42
#define PIN_I2S_DATA   39
#define I2S_PORT       I2S_NUM_0

// ============================================================
// 音频 + FFT 参数
// ============================================================
#define SAMPLE_RATE     16000   // 16kHz 对音乐频谱足够
#define FFT_SIZE        1024    // 1024 点 FFT → 512 bins
#define DISPLAY_BARS    32      // 最终显示 32 根柱子

// ============================================================
// 显示参数
// ============================================================
#define LCD_WIDTH       320
#define LCD_HEIGHT      320
#define LVGL_BUF_HEIGHT 40      // 双缓冲：320×40 每块

// ============================================================
// FreeRTOS 任务参数
// ============================================================
// Core 0: 音频采集 + FFT
#define AUDIO_TASK_STACK      8192
#define AUDIO_TASK_PRIO       5
#define AUDIO_TASK_CORE       0

// Core 0: 频谱数据刷新
#define UI_TASK_STACK         4096
#define UI_TASK_PRIO          3
#define UI_TASK_CORE          0

// Core 1: LVGL 显示渲染
#define DISPLAY_TASK_STACK    8192
#define DISPLAY_TASK_PRIO     2
#define DISPLAY_TASK_CORE     1
