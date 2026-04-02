idf_component_register(
    SRCS "fall_detection_ai.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        lexacare_types
        freertos
        esp_system
        esp_timer
)
# Note : quand le modèle ESP-DL est activé (CONFIG_AI_USE_ESPDL_MODEL=y),
# ajouter "espressif__esp-dl" à la liste REQUIRES après installation
# du composant via idf_component.yml.
