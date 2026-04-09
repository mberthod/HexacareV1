idf_component_register(
    SRCS "hw_diag.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lexacare_types
        esp_driver_i2c
        esp_driver_spi
        esp_driver_uart
        esp_driver_gpio
        cjson
        esp_timer
        freertos
        esp_system
)
