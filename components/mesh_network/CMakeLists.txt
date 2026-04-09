idf_component_register(
    SRCS
        "mesh_manager.c"
        "ota_node.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lexacare_types
        esp_wifi
        nvs_flash
        esp_netif
        mbedtls
        app_update
        freertos
        esp_system
        esp_timer
        esp_hw_support
)
