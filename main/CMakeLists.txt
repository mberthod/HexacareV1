idf_component_register(
    SRCS "app_main.c"
         "task_audio.c"
         "task_vision.c"
         "orchestrator.c"
         "task_capture.c"
         "task_usb_telemetry.c"
         "lexa_tof_frame_ascii.c"
         "sensors_board.c"
         "task_mfcc_debug.c"
         "task_mesh_bench.c"
    INCLUDE_DIRS "."
    REQUIRES mesh_manager
             tr_espnow
             tr_wifimesh
             i2s_stereo_mic
             mfcc_dsp
             tflm_dual_runtime
             vl53l8cx_array
             pca9555_io
             esp_wifi
             nvs_flash
             esp_event
             esp_netif
             driver
             esp_driver_gpio
)
