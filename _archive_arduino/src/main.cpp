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
#include "mesh/espnow_mesh.h"
#include "mesh/routing_manager.h"
#include "OTA/official_ota_manager.h"
#include "mesh/serial_gateway.h"
#include "rtos/queues_events.h"
#include "sensors/sensor_sim.h" /* sensor_sim_get_task_handle() pour OTA locale */
#include <Wire.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <freertos/task.h>

// #define TEST_LED_PIN PIN_RGB_LED
// Adafruit_NeoPixel pixel(1, TEST_LED_PIN, NEO_GRB + NEO_KHZ800);

static const char *TAG_MAIN = "MAIN";

/** Rôle ROOT/NODE : défini au tout début de setup() par lecture de GPIO 1 (pull-up ; LOW = ROOT). */
bool g_lexacare_this_node_is_gateway = false;

// Callback ESP-NOW global (redirection vers espnow_mesh.cpp qui gère le dispatch)
// Note: Dans la nouvelle architecture, c'est espnow_mesh.cpp qui enregistre le callback.
// Donc on n'a plus besoin de on_espnow_recv ici.

// Tâche d'envoi des données capteurs (générées localement)
/**
 * @brief Tâche d'émission des données capteurs vers le parent ou la passerelle série.
 *
 * @details
 * Cette tâche consomme les trames LexaFullFrame_t déposées dans la file g_queue_espnow_tx.
 * - Si le nœud est ROOT (passerelle), les données sont sérialisées et envoyées sur la liaison série au PC.
 * - Sinon, les trames sont transmises par unicast ESP-NOW au parent mesh.
 * Avant l'envoi, la couche (layer) et l'identifiant du parent (parentId) sont injectés dans la trame,
 * puis le CRC est recalculé.
 * Un feedback visuel (led_flash_yellow_rx) est déclenché à chaque envoi.
 *
 * Fonctionne en tâche FreeRTOS, blocante sur la queue.
 *
 * @param pv Paramètre inutilisé (conforme FreeRTOS).
 */

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
            if (g_lexacare_this_node_is_gateway)
            {
                serial_gateway_send_data_json(&frame);
                led_flash_yellow_rx();        // Feedback visuel (simulé comme réception locale)
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
    /* Dès le démarrage : GPIO 1 en entrée avec pull-up. LOW = ROOT, HIGH / non câblé = NODE. */
    pinMode(PIN_ROOT_NODE_SEL, INPUT_PULLUP);
    delay(5); /* Laisse le pull-up se stabiliser */
    g_lexacare_this_node_is_gateway = (digitalRead(PIN_ROOT_NODE_SEL) == LOW);

    delay(1000);
    // Serial.begin(921600); // Déjà fait dans log_dual_init si activé, ou serial_gateway_init
    log_dual_init();
    /* Marquer cette app comme valide pour éviter un rollback après OTA */
    esp_ota_mark_app_valid_cancel_rollback();
    /* Afficher la cause du reset précédent (aide au debug des boot loops) */
    switch (esp_reset_reason())
    {
    case ESP_RST_PANIC:
        log_dual_println("[BOOT] Reset precedent: PANIC/Exception");
        break;
    case ESP_RST_INT_WDT:
        log_dual_println("[BOOT] Reset precedent: Watchdog interrupt");
        break;
    case ESP_RST_TASK_WDT:
        log_dual_println("[BOOT] Reset precedent: Watchdog tâche");
        break;
    case ESP_RST_WDT:
        log_dual_println("[BOOT] Reset precedent: Watchdog");
        break;
    case ESP_RST_BROWNOUT:
        log_dual_println("[BOOT] Reset precedent: Brownout");
        break;
    case ESP_RST_SW:
        log_dual_println("[BOOT] Reset precedent: Software");
        break;
    case ESP_RST_POWERON:
        log_dual_println("[BOOT] Reset precedent: Power-on");
        break;
    default:
        log_dual_printf("[BOOT] Reset precedent: %d\r\n", (int)esp_reset_reason());
        break;
    }
    log_dual_println("[BOOT] Lexacare V2 - Tree Mesh");
    log_dual_printf("[BOOT] Serial USB @ %u baud - debug actif\r\n", (unsigned)LOG_DUAL_BAUD);
    log_dual_printf("[BOOT] CURRENT_FW_VERSION=%u\r\n", (unsigned)CURRENT_FW_VERSION);
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (running)
    {
        log_dual_printf("[BOOT] Running partition: label=%s addr=0x%lx size=%lu\r\n",
                        running->label,
                        (unsigned long)running->address,
                        (unsigned long)running->size);
    }
    if (boot)
    {
        log_dual_printf("[BOOT] Boot partition: label=%s addr=0x%lx size=%lu\r\n",
                        boot->label,
                        (unsigned long)boot->address,
                        (unsigned long)boot->size);
    }

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

    official_ota_init();

    routing_init();
/*
 * ROOT (Node 0) : pont série. Sortie = JSON pour MSG_TYPE_DATA.
 *
 * @details
 * Cette section configure le noeud comme ROOT (chef de réseau) dans le mesh Lexacare Tree.
 * Elle doit être appelée uniquement sur le noeud passerelle (Gateway), c'est-à-dire l'unique racine du réseau.
 *
 * **Effets principaux :**
 * - Attribue au noeud la couche 0 (s_my_layer = 0), signifiant qu'il est la racine du réseau mesh.
 * - Met à jour l'état interne du routeur (s_state) en le passant à STATE_CONNECTED, indiquant une connexion stable.
 * - Déclenche l'animation LED spéciale "ROOT" via led_manager_set_state(LED_STATE_ROOT) pour signaler visuellement à l'utilisateur
 *   que ce noeud est désormais chef du réseau.
 * - Affiche un message informatif dans les logs série via ESP_LOGI, avec confirmation d'entrée en mode ROOT.
 *
 * **Utilisation typique :**
 * - Appelée dans la séquence d'initialisation du firmware lorsque le flag g_lexacare_this_node_is_gateway est à true.
 *
 * @see routing_set_root()
 * @see led_manager_set_state()
 * @see g_lexacare_this_node_is_gateway
 * @see STATE_CONNECTED
 */
    if (g_lexacare_this_node_is_gateway)
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
        official_ota_responder_start();
    }
    /*
     * ---------------------------------------------------------------------------
     *  RÉPARTITION DES TÂCHES ENTRE LES DEUX CŒURS DE L'ESP32-S3 :
     *
     *  - Core 0 (Pro CPU) : Gestion du réseau, de la topologie Mesh, OTA, transfert radio.
     *      -> routing_task         : Découverte et gestion du réseau Mesh, parent/enfants.
     *      -> dataTxTask           : Expédition des trames capteurs/diagnostic au parent ou Root (Gateway).
     *      -> serial_gateway_task* : Interface USB avec le backend si GATEWAY.
     *
     *  - Core 1 (App CPU) : Traitement temps réel des capteurs, gestion de la LED, et tâches accessoires.
     *      -> led_manager_task      : Animation RGB de la LED en fonction de l’état du système/statut radio.
     *      -> sensor_sim_task_start : Injection de données simulées pour tests/émulation capteurs.
     *      -> (Et toutes futures tâches d'analyse capteurs temps réel)
     *
     *  => Cette organisation permet de garantir que le traitement radio (WiFi/ESP-NOW)
     *     - critique en temps réel - n’est jamais bloqué par la lecture ou le calcul intensif
     *     sur les capteurs ou par l’animation de la LED RGB.
     *
     *  Accès aux données partagées :
     *      - Toutes les interactions avec l’état global du système utilisent
     *        le module 'system_state' (thread-safe avec mutex FreeRTOS).
     *      - La communication inter-tâches utilise files (Queues) et EventGroups.
     * ---------------------------------------------------------------------------
     */

    // ----------------------------------------------------------------------------------------
    // Tâches FreeRTOS principales lancées au démarrage :
    //
    // 1. led_manager_task (Core 1)
    //    - Gère l'affichage de la LED RGB en fonction de l'état du système.
    //    - Assure la signalisation des flashs RX/TX, l'état connecté, le mode racine, les erreurs, etc.
    //    - Met à jour la LED à une fréquence rapide sans bloquer la radio.
    //
    // 2. routing_task (Core 0)
    //    - S'occupe de la découverte des voisins Mesh, du suivi de la topologie réseau, et des changements de parent.
    //    - Gère la remontée/descente d'informations pour trouver le ROOT du réseau.
    //
    // 3. ota_tree_task (Core 0)
    //    - Supervise la distribution des mises à jour OTA via le Tree Mesh.
    //    - Peut propager les paquets OTA à travers la hiérarchie des nœuds.
    //
    // 4. dataTxTask (Core 0)
    //    - S'occupe de l'envoi asynchrone des données capteurs et trames de diagnostic vers le parent ou le gateway.
    //    - Récupère les données à transmettre via la file g_queue_espnow_tx.
    //
    // 4. sensor_sim_task_start (Core 1)
    //    - Simule les capteurs pour des tests (Lidar, Radar, Audio, etc.) puis injecte les données dans le flot normal.
    //    - Peut être remplacé par les vraies tâches capteurs sur une cible réelle.
    //
    // 5. serial_gateway_task (Core 0, seulement sur GATEWAY)
    //    - Gère l'interface série USB pour connexion avec le backend ou outils PC.
    //    - Permet la récupération des logs, la topologie du mesh, le flash OTA, etc.
    //
    // Notes :
    //  - Les tâches sont réparties sur les deux cœurs pour garantir la réactivité de la radio.
    //  - Tous les accès à l’état système global sont protégés par mutex (voir system_state).
    // ----------------------------------------------------------------------------------------
    TaskHandle_t h_routing = NULL;
    TaskHandle_t h_data_tx = NULL;

    xTaskCreatePinnedToCore(led_manager_task, "LedMgr", 2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(routing_task, "Routing", 4096, NULL, 3, &h_routing, 0);
    xTaskCreatePinnedToCore(dataTxTask, "DataTx", 3072, NULL, 1, &h_data_tx, 0);

    sensor_sim_task_start();
    // Cette section lance toutes les tâches principales du firmware, chacune étant dédiée à une responsabilité précise.
    // Par exemple, la tâche led_manager_task, épinglée sur le Core 1, gère la LED RGB en actualisant son état toutes les 50ms :
    // - Elle affiche différentes couleurs et effets (clignotement, pulsation, changement rapide) pour indiquer les états importants du système,
    //   comme la connexion réseau, la détection d'erreur, le mode OTA, ou la réception/envoi de messages.
    // - Un système de flash prioritaire permet à la LED d'indiquer immédiatement une communication RX/TX,
    //   en interrompant brièvement l'effet visuel courant pour afficher une couleur dédiée durant 50ms.
    // - La logique inclut aussi des effets de pulsation au lieu de simples allumages fixes afin de rendre les changements d'état plus perceptibles.
    // Le découplage de cette gestion sur un cœur dédié évite ainsi de perturber la radio ou d'introduire de la latence dans le traitement des capteurs.

    if (g_lexacare_this_node_is_gateway)
    {
        xTaskCreatePinnedToCore(serial_gateway_task, "SerialGW", 8192, NULL, 2, NULL, 0);
        serial_gateway_register_tasks_for_ota_suspend(h_routing, NULL, h_data_tx, sensor_sim_get_task_handle());
    }

    log_dual_println("[BOOT] System Ready");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
