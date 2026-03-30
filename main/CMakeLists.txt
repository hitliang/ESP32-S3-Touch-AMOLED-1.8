idf_component_register(
    SRCS "main.c" "display.c" "touch.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        nvs_flash
        freertos
        tca9554
)
