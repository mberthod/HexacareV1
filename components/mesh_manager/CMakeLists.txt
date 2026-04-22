idf_component_register(
    SRCS "src/mesh_manager.c"
         "src/failover_policy.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_wifi
             esp_event
             esp_timer
             nvs_flash
    PRIV_REQUIRES tr_espnow
                  tr_wifimesh
)
