idf_component_register(
    SRCS "src/tr_espnow.c"
         "src/espnow_tree.c"
         "src/espnow_neighbor.c"
         "src/espnow_msg.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "include_priv" "src"
    REQUIRES esp_wifi
             esp_timer
    PRIV_REQUIRES esp_event
                  esp_system
)
