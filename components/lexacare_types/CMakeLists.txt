idf_component_register(
    # Composant en-tête uniquement : pas de source C
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_driver_spi
)
