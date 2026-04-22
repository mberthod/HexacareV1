idf_component_register(
    SRCS "src/tr_wifimesh.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi
             esp_event
    PRIV_REQUIRES esp_netif
)
