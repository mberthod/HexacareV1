set(SENSOR_ACQ_SRCS
    "lidar_driver.c"
    "radar_driver.c"
    "vl53l8cx_platform.c"
    "hdc1080_driver.c"
    "bme280_driver.c"
    "mic_driver.c"
)

# ULD ST VL53L8CX (STSW-IMG040) — code propriétaire non inclus dans ce repo.
# Si l'utilisateur a copié l'ULD dans components/sensor_acq/uld/, on l'embarque automatiquement.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/uld/vl53l8cx_api.c")
    file(GLOB SENSOR_ACQ_ULD_SRCS "${CMAKE_CURRENT_LIST_DIR}/uld/*.c")
    foreach(_src IN LISTS SENSOR_ACQ_ULD_SRCS)
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_LIST_DIR}" "${_src}")
        list(APPEND SENSOR_ACQ_SRCS "${_rel}")
    endforeach()
    message(STATUS "sensor_acq: ULD ST VL53L8CX détecté (uld/...)")
endif()

idf_component_register(
    SRCS ${SENSOR_ACQ_SRCS}
    INCLUDE_DIRS "include" "uld"
    REQUIRES
        lexacare_types
        heap
        esp_driver_spi
        esp_driver_uart
        esp_driver_i2c
        esp_driver_i2s
        esp_timer
        freertos
        esp_system
)

# Flag de compilation si l'ULD ST est présent.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/uld/vl53l8cx_api.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LEXACARE_HAVE_ST_ULD=1)
endif()
