idf_component_register(
    SRCS
        "main.c"
        "audio_fft.c"
        "lcd_st7796.c"
        "spectrum_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp-dsp
        esp_timer
        spi_flash
        lvgl
)
