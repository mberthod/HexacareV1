idf_component_register(
    SRCS
        "littlefs_manager.c"
        "log_writer.c"
        "panic_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lexacare_types
        spiffs
        esp_system
        freertos
        spi_flash
)
