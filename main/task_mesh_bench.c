/* task_mesh_bench.c — heartbeat BENCH: + envoi DATA périodique (mode test_mesh) */
#include "app_config.h"
#include "mesh_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "task_helpers.h"

static const char *TAG = "mesh_bench";

#if MBH_MODE_TEST_MESH || MBH_MODE_MESH_ONLY
static void mesh_bench_entry(void *arg)
{
    (void)arg;
    uint32_t seq = 0;

    for (;;) {
#if MBH_MODE_TEST_MESH
        uint8_t pl[8] = {
            'H', 'B',
            (uint8_t)(APP_NODE_ID & 0xFF),
            (uint8_t)((APP_NODE_ID >> 8) & 0xFF),
            (uint8_t)(seq & 0xFF),
            (uint8_t)((seq >> 8) & 0xFF),
            (uint8_t)((seq >> 16) & 0xFF),
            (uint8_t)((seq >> 24) & 0xFF),
        };
        mesh_msg_t m = {
            .dst_node_id = MESH_NODE_BROADCAST,
            .payload     = pl,
            .len         = sizeof(pl),
            .prio        = MESH_PRIO_DATA,
            .flags       = 0,
        };
        esp_err_t err = mesh_send(&m);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mesh_send HB: %s", esp_err_to_name(err));
        }
#endif
        ESP_LOGI(
            "BENCH",
            "BENCH:{\"node_id\":%u,\"seq\":%lu,\"transport\":%d}",
            (unsigned)APP_NODE_ID,
            (unsigned long)seq,
            (int)mesh_manager_status());
        seq++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif

void task_mesh_bench_start(void)
{
#if MBH_MODE_TEST_MESH || MBH_MODE_MESH_ONLY
    BaseType_t ok = xTaskCreatePinnedToCore(
        mesh_bench_entry, "mesh_bench", 4096, NULL, 2, NULL, 0);
    configASSERT(ok == pdPASS);
#else
    (void)0;
#endif
}
