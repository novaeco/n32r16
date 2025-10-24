idf_component_register(SRCS "esp_lcd_panel_rgb.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_lcd driver freertos log)
