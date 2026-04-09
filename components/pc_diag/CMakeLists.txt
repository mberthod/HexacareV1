idf_component_register(
    SRCS "pc_diag.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lexacare_types
        sys_storage
        cjson
        freertos
        esp_system
        esp_timer
        spi_flash
)
