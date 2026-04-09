idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        lexacare_types
        hw_diag
        sensor_acq
        esp_driver_i2c
        ai_engine
        mesh_network
        pc_diag
        sys_storage
        nvs_flash
        esp_event
        esp_system
        heap
        freertos
        log
)
