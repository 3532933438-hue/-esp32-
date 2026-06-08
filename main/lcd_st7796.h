#pragma once
#include "lvgl.h"

void lcd_lvgl_init(void);
void lvgl_set_display_driver(lv_disp_drv_t *drv);
void lvgl_tick_init(void);
void display_task(void *arg);
