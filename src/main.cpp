/**
 * @file main.cpp
 * @brief Le Chef d'Orchestre du système Lexacare (Point d'entrée).
 * 
 * @details
 * Imaginez ce fichier comme le chef d'orchestre d'un grand concert.
 * Son rôle est de :
 * 1. Préparer la scène (Initialiser le matériel : mémoire, LEDs, Radio).
 * 2. Recruter les musiciens (Créer les "Tâches" indépendantes qui font le travail).
 * 3. Donner le coup d'envoi (Lancer le système).
 * 
 * Il ne joue pas les instruments lui-même, mais il s'assure que tout le monde est prêt
 * et synchronisé.
 * 
 * Les "Musiciens" (Tâches) qu'il recrute sont :
 * - `routing_task` : Le GPS, qui gère la carte du réseau.
 * - `ota_tree_task` : Le Facteur, qui distribue les mises à jour logicielles.
 * - `dataTxTask` : L'Émetteur, qui envoie les données des capteurs.
 * - `led_manager_task` : L'Éclairagiste, qui change la couleur de la LED.
 * - `serial_gateway_task` (si ROOT) : Le Traducteur, qui parle au PC.
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config/config.h"
#include "config/pins_lexacare.h"
#include "system/log_dual.h"
#include "system/led_manager.h"
#include "lexacare_protocol.h"
#include "comm/espnow_mesh.h"
#include "comm/routing_manager.h"
#include "comm/ota_tree_manager.h"
#include "comm/serial_gateway.h"
#include "rtos/queues_events.h"
#include "sensors/sensor_sim.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <freertos/task.h>

// #define TEST_LED_PIN PIN_RGB_LED
// Adafruit_NeoPixel pixel(1, TEST_LED_PIN, NEO_GRB + NEO_KHZ800);

static const char *TAG_MAIN = "MAIN";

// Callback ESP-NOW global (redirection vers espnow_mesh.cpp qui gère le dispatch)
// Note: Dans la nouvelle architecture, c'est espnow_mesh.cpp qui enregistre le callback.
// Donc on n'a plus besoin de on_espnow_recv ici.

// Tâche d'envoi des données capteurs (générées localement)
static void dataTxTask(void *pv)
{
    LexaFullFrame_t frame;
    while (1)
    {
        if (xQueueReceive(g_queue_espnow_tx, &frame, portMAX_DELAY) == pdTRUE)
        {
            // Injecter les infos de routage avant l'envoi
            frame.layer = routing_get_layer();
            frame.parentId = routing_get_parent_id();
            lexaframe_fill_crc(&frame); // Recalculer le CRC car on a modifié la trame

            // Si ROOT, direct série
            if (LEXACARE_THIS_NODE_IS_GATEWAY)
            {
                serial_gateway_send_data_json(&frame, 0);
                led_flash_yellow_rx(); // Feedback visuel (simulé comme réception locale)
                vTaskDelay(pdMS_TO_TICKS(1)); /* Yield pour éviter WDT (Serial peut bloquer) */
            }
            else
            {
                // Sinon, envoyer au parent
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac))
                {
                    routing_send_unicast(parent_mac, MSG_DATA, (uint8_t *)&frame, sizeof(frame));
                    led_flash_yellow_rx();
                }
            }
        }
    }
}

void setup()
{
    delay(1000);
    // Serial.begin(921600); // Déjà fait dans log_dual_init si activé, ou serial_gateway_init
    log_dual_init();
    /* Marquer cette app comme valide pour éviter un rollback après OTA */
    esp_ota_mark_app_valid_cancel_rollback();
    /* Afficher la cause du reset précédent (aide au debug des boot loops) */
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:    log_dual_println("[BOOT] Reset precedent: PANIC/Exception"); break;
        case ESP_RST_INT_WDT:  log_dual_println("[BOOT] Reset precedent: Watchdog interrupt"); break;
        case ESP_RST_TASK_WDT: log_dual_println("[BOOT] Reset precedent: Watchdog tâche"); break;
        case ESP_RST_WDT:      log_dual_println("[BOOT] Reset precedent: Watchdog"); break;
        case ESP_RST_BROWNOUT: log_dual_println("[BOOT] Reset precedent: Brownout"); break;
        case ESP_RST_SW:      log_dual_println("[BOOT] Reset precedent: Software"); break;
        case ESP_RST_POWERON:  log_dual_println("[BOOT] Reset precedent: Power-on"); break;
        default:              log_dual_printf("[BOOT] Reset precedent: %d\r\n", (int)esp_reset_reason()); break;
    }
    log_dual_println("[BOOT] Lexacare V2 - Tree Mesh");
    log_dual_printf("[BOOT] Serial USB @ %u baud - debug actif\r\n", (unsigned)LOG_DUAL_BAUD);

    // Init LED Manager
    led_manager_init();
    led_manager_set_state(LED_STATE_SCANNING); // État initial

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialisation Modules
    queues_events_init();

    // Init Radio (WiFi + ESP-NOW)
    if (!espnow_mesh_init())
    {
        ESP_LOGE(TAG_MAIN, "ESP-NOW Init Failed");
        led_manager_set_state(LED_STATE_ERROR);
        return;
    }
    ESP_LOGI(TAG_MAIN, "ESP-NOW Init OK (Channel %d)", ESPNOW_CHANNEL);

    routing_init();
    ota_tree_init();

    if (LEXACARE_THIS_NODE_IS_GATEWAY)
    {
        routing_set_root(); // Configurer comme ROOT pour le mesh
        serial_gateway_init();
        led_manager_set_state(LED_STATE_ROOT);
        log_dual_println("[BOOT] Mode ROOT");
    }
    else
    {
        led_manager_set_state(LED_STATE_SCANNING);
        log_dual_println("[BOOT] Mode NODE");
    }

    // Tâches
    xTaskCreatePinnedToCore(led_manager_task, "LedMgr", 2048, NULL, 1, NULL, 1); // Core 1 pour ne pas bloquer radio
    xTaskCreatePinnedToCore(routing_task, "Routing", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(ota_tree_task, "OTA_Tree", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(dataTxTask, "DataTx", 3072, NULL, 1, NULL, 0);

    // Simulation Capteurs (Core 1) - envoie des trames vers série (ROOT) ou parent (NODE)
    sensor_sim_task_start();

    if (LEXACARE_THIS_NODE_IS_GATEWAY)
    {
        // Serial Gateway pour OTA UART (Core 0) - pile 6 Ko (buffers JSON + topologie)
        xTaskCreatePinnedToCore(serial_gateway_task, "SerialGW", 6144, NULL, 2, NULL, 0);
    }

    log_dual_println("[BOOT] System Ready");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
