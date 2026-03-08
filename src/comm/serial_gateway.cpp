/**
 * @file serial_gateway.cpp
 * @brief L'Interprète (Passerelle Série USB).
 * 
 * @details
 * Ce fichier ne sert QUE pour le boîtier "ROOT" (celui branché au PC).
 * Il agit comme un traducteur bilingue entre le PC (Python) et le Réseau Mesh (C++).
 * 
 * Il a deux missions principales :
 * 
 * 1. **Sens Montant (Données -> PC)** :
 *    - Il reçoit les données brutes (binaires) des capteurs du réseau.
 *    - Il les traduit en texte lisible (JSON) pour que le logiciel PC puisse afficher les graphiques.
 *    - Exemple : `0x1A 0x02...` devient `{"temp": 26.2, "heartRate": 70}`.
 * 
 * 2. **Sens Descendant (Mise à jour -> Réseau)** :
 *    - Il écoute le PC qui envoie le nouveau logiciel (Firmware).
 *    - Il découpe ce logiciel en petits paquets et les donne au gestionnaire de mise à jour (`ota_tree_manager`).
 */

#include "comm/serial_gateway.h"
#include "comm/ota_tree_manager.h"
#include "comm/routing_manager.h"
#include "config/config.h"
#include "system/led_manager.h"
#include "lexacare_protocol.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#define TOPO_MAX 64
#define TOPO_INTERVAL_MS 5000

/** Cache topologie pour envoi périodique [TOPOLOGY] au script Python. */
struct TopoNode {
    uint16_t id;
    uint16_t parentId;
    uint8_t  layer;
    uint32_t lastSeen;
};
static TopoNode s_topo[TOPO_MAX];
static int s_topo_n = 0;
static TickType_t s_last_topology_tick = 0;

#define OTA_CHUNK_PREFIX "OTA_CHUNK:"
#define OTA_HEX_LEN 400

// Mode binaire : 0x01 = OTA Série (ROOT se flashe), 0x02 = OTA Mesh (ROOT diffuse)
// Dans V2, les deux modes impliquent : Stockage local -> Distribution Arbre
#define OTA_SERIAL_MODE_ROOT 0x01
#define OTA_SERIAL_MODE_MESH 0x02

enum SerialGatewayState {
    SERIAL_STATE_IDLE = 0,
    SERIAL_STATE_OTA_HEADER,
    SERIAL_STATE_OTA_CHUNK
};

static bool s_serial_gateway_init_done = false;
static uint32_t s_msg_id_seq = 0;
/** True pendant la réception OTA Série (0x01 + header + chunks) : le mesh ne doit pas injecter OTA dans ota_mesh. */
static volatile bool s_ota_serial_receiving = false;

// Handles des tâches à suspendre (non utilisé dans V2 car ota_tree_manager gère la concurrence via état)
// static TaskHandle_t s_task_mesh = nullptr;
// static TaskHandle_t s_task_tx = nullptr;
// static TaskHandle_t s_task_sensor = nullptr;

/**
 * @brief Vérifie si une mise à jour est en cours de réception par le câble USB.
 *
 * @details
 * Cette fonction est comme un feu rouge/vert pour le reste du système.
 * Si elle renvoie `1` (Vrai), cela veut dire : "Silence ! Le PC est en train de parler,
 * n'essayez pas de faire autre chose."
 *
 * @return 1 si le PC envoie une mise à jour, 0 sinon.
 */
int serial_gateway_is_ota_serial_receiving(void) {
    return s_ota_serial_receiving ? 1 : 0;
}

/**
 * @brief Enregistre les tâches à mettre en pause (Obsolète).
 *
 * @details
 * Cette fonction servait dans l'ancienne version à dire "Chut !" aux autres ouvriers.
 * Dans la nouvelle version (V2), les ouvriers sont assez intelligents pour se taire tout seuls
 * quand ils voient que le chef est occupé.
 * On la garde pour la compatibilité, mais elle ne fait plus grand-chose.
 */
void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t mesh_handle, TaskHandle_t tx_handle, TaskHandle_t sensor_handle) {
    // s_task_mesh = mesh_handle;
    // s_task_tx = tx_handle;
    // s_task_sensor = sensor_handle;
}

/**
 * @brief Traducteur Hexadécimal vers Binaire.
 *
 * @details
 * Le PC envoie parfois des données sous forme de texte "A1 B2 C3" (Hexadécimal).
 * Cette fonction transforme ce texte en vrais nombres que l'ordinateur comprend (Binaire).
 * C'est comme traduire "Douze" en `12`.
 *
 * @param hex Le texte reçu (ex: "A1").
 * @param hex_len La longueur du texte.
 * @param out Où stocker le résultat.
 * @param out_size La place disponible pour le résultat.
 * @return Le nombre d'octets traduits.
 */
static int hex_to_binary(const char *hex, size_t hex_len, uint8_t *out, size_t out_size) {
    if (hex_len % 2 != 0 || out_size < hex_len / 2) return -1;
    for (size_t i = 0; i < hex_len && i / 2 < out_size; i += 2) {
        char c1 = hex[i], c2 = hex[i + 1];
        int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : (c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : -1;
        int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : (c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : -1;
        if (v1 < 0 || v2 < 0) return -1;
        out[i / 2] = (uint8_t)((v1 << 4) | v2);
    }
    return (int)(hex_len / 2);
}

/**
 * @brief Envoi des données capteurs au PC (Format JSON).
 *
 * @details
 * C'est ici que le ROOT prend les données brutes reçues du réseau et les transforme
 * en un format que le logiciel PC (Python) adore : le JSON.
 *
 * Exemple :
 * - Entrée (Brut) : `[0x01, 0x1A, 0x45...]`
 * - Sortie (JSON) : `{"temp": 26.5, "batt": 3700}`
 *
 * @param frame Les données brutes.
 * @param fw_ver La version du logiciel (pour info).
 */
/** Enregistre un nœud dans le cache topologie (pour [TOPOLOGY]). */
static void topology_add_node(uint16_t id, uint16_t parentId, uint8_t layer) {
    uint32_t now = xTaskGetTickCount();
    for (int i = 0; i < s_topo_n; i++) {
        if (s_topo[i].id == id) {
            s_topo[i].parentId = parentId;
            s_topo[i].layer = layer;
            s_topo[i].lastSeen = now;
            return;
        }
    }
    if (s_topo_n < TOPO_MAX) {
        s_topo[s_topo_n].id = id;
        s_topo[s_topo_n].parentId = parentId;
        s_topo[s_topo_n].layer = layer;
        s_topo[s_topo_n].lastSeen = now;
        s_topo_n++;
    }
}

void serial_gateway_send_data_json(const void *frame, uint32_t fw_ver) {
    const LexaFullFrame_t *f = (const LexaFullFrame_t *)frame;
    topology_add_node((uint16_t)f->nodeShortId, (uint16_t)f->parentId, (uint8_t)f->layer);

    StaticJsonDocument<512> doc;
    
    // Infos Topologie (V2)
    doc["nodeId"] = (unsigned)f->nodeShortId;
    doc["parentId"] = (unsigned)f->parentId;
    doc["layer"] = (unsigned)f->layer;
    
    doc["epoch"] = f->epoch;
    doc["probFallLidar"] = f->probFallLidar;
    doc["probFallAudio"] = f->probFallAudio;
    doc["heartRate"] = f->heartRate;
    doc["respRate"] = f->respRate;
    doc["tempExt"] = (float)f->tempExt / 100.0f;
    doc["humidity"] = (float)f->humidity / 100.0f;
    doc["pressure"] = 800.0f + (float)f->pressure / 10.0f;
    doc["thermalMax"] = (float)f->thermalMax / 100.0f;
    doc["volumeOccupancy"] = f->volumeOccupancy;
    doc["vBat"] = f->vBat;
    doc["sensorFlags"] = f->sensorFlags;
    doc["fw_ver"] = (unsigned)fw_ver;
    
    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0) {
        Serial.println(buf);
    }
}

/**
 * @brief Analyseur de ligne de texte (Obsolète).
 *
 * @details
 * Ancienne méthode pour recevoir les mises à jour en mode texte ("OTA_CHUNK:1:50...").
 * Elle est conservée au cas où, mais la V2 utilise maintenant le mode binaire (0x02)
 * qui est beaucoup plus rapide et fiable.
 */
static void process_line(char *line, size_t len) {
    if (len < sizeof(OTA_CHUNK_PREFIX) - 1) return;
    if (memcmp(line, OTA_CHUNK_PREFIX, sizeof(OTA_CHUNK_PREFIX) - 1) != 0) return;
    char *p = line + sizeof(OTA_CHUNK_PREFIX) - 1;
    unsigned long idx = strtoul(p, &p, 10);
    if (*p != ':') return;
    p++;
    unsigned long total = strtoul(p, &p, 10);
    if (*p != ':') return;
    p++;
    if (idx > 0xFFFF || total > 0xFFFF || total == 0) return;
    size_t hex_len = strlen(p);
    while (hex_len > 0 && (p[hex_len - 1] == '\r' || p[hex_len - 1] == '\n')) hex_len--;
    if (hex_len != OTA_HEX_LEN) return;

    uint8_t data[OTA_CHUNK_DATA_SIZE];
    int bin_len = hex_to_binary(p, hex_len, data, sizeof(data));
    if (bin_len != (int)OTA_CHUNK_DATA_SIZE) return;

    // TODO: Adapter pour OTA Tree Manager si on veut supporter le mode texte
    // Pour l'instant, on se concentre sur le mode binaire (0x01/0x02)
    // OtaChunkPayload payload;
    // payload.chunkIndex = (uint16_t)idx;
    // payload.totalChunks = (uint16_t)total;
    // memcpy(payload.data, data, OTA_CHUNK_DATA_SIZE);
    
    // ...
}

/**
 * @brief Initialisation de la Passerelle Série.
 *
 * @details
 * Prépare le port USB pour communiquer avec le PC à haute vitesse.
 * Elle augmente aussi la taille de la "boîte aux lettres" (Buffer) pour ne pas perdre
 * de messages si le PC parle trop vite.
 */
int serial_gateway_init(void) {
    Serial.setRxBufferSize(4096); // Augmenter buffer pour OTA rapide
    s_serial_gateway_init_done = true;
    return 1;
}

/**
 * @brief La Tâche de la Passerelle Série (L'Oreille du PC).
 *
 * @details
 * Cette tâche écoute en permanence ce que dit le PC.
 *
 * Elle a deux modes de fonctionnement :
 * 1. **Mode Repos (IDLE)** : Elle attend un ordre spécial. Si elle reçoit le code secret `0x02`,
 *    elle comprend que le PC veut envoyer une mise à jour et passe en mode OTA.
 * 2. **Mode OTA (Mise à jour)** :
 *    - Elle lit d'abord l'en-tête (la description du fichier).
 *    - Puis elle lit le fichier morceau par morceau (Chunks).
 *    - Elle donne chaque morceau au `ota_tree_manager` pour qu'il le stocke.
 *
 * C'est grâce à elle que vous pouvez mettre à jour tout le réseau depuis votre bureau.
 */
void serial_gateway_task(void *pv) {
    (void)pv;
    /* Éviter d'envoyer [TOPOLOGY] dès la 1re boucle (risque stack + WDT) */
    s_last_topology_tick = xTaskGetTickCount();
    log_dual_println("[TASK] serial_gateway running (Core 0)");
    
    static char line_buf[600];
    size_t pos = 0;
    
    enum SerialGatewayState state = SERIAL_STATE_IDLE;
    static uint8_t ota_buf[256];
    size_t ota_count = 0;
    
    // Métadonnées OTA
    uint32_t ota_total_size = 0;
    uint16_t ota_total_chunks = 0;
    char ota_md5[33] = {0};
    uint16_t ota_chunk_idx = 0;

    for (;;) {
        if (state == SERIAL_STATE_IDLE) {
            /* Envoi périodique [TOPOLOGY] pour l’interface Python (architecture réseau) */
            TickType_t now_tick = xTaskGetTickCount();
            if ((now_tick - s_last_topology_tick) >= pdMS_TO_TICKS(TOPO_INTERVAL_MS)) {
                s_last_topology_tick = now_tick;
                StaticJsonDocument<1024> topo_doc;
                topo_doc["root"] = (unsigned)routing_get_my_id();
                JsonArray arr = topo_doc.createNestedArray("nodes");
                JsonObject root_node = arr.createNestedObject();
                root_node["id"] = (unsigned)routing_get_my_id();
                root_node["parentId"] = (uint32_t)0xFFFF;  /* null = pas de parent */
                root_node["layer"] = 0;
                for (int i = 0; i < s_topo_n; i++) {
                    JsonObject n = arr.createNestedObject();
                    n["id"] = (unsigned)s_topo[i].id;
                    n["parentId"] = (unsigned)s_topo[i].parentId;
                    n["layer"] = (unsigned)s_topo[i].layer;
                }
                char topo_buf[1024];
                size_t topo_len = serializeJson(topo_doc, topo_buf, sizeof(topo_buf));
                if (topo_len > 0) {
                    Serial.print("[TOPOLOGY]");
                    Serial.println(topo_buf);
                    vTaskDelay(pdMS_TO_TICKS(5)); /* Yield pour éviter WDT */
                }
            }

            // Lire par blocs pour éviter de bloquer la tâche si flux continu de données (bruit/logs)
            int bytesRead = 0;
            while (Serial.available() > 0 && bytesRead < 128) {
                int b = Serial.read();
                bytesRead++;

                // Processus OTA distincts : 0x01 = ROOT seul (série), 0x02 = tous les nœuds (mesh)
                if (b == 0x01) {
                    log_dual_printf("[SERIE] OTA 0x01 = ROOT seul (LED violet). Attente en-tete 38 octets...\r\n");
                    s_ota_serial_receiving = true;
                    ota_tree_set_uart_mode(0x01);
                    ota_tree_init();
                    led_manager_set_state(LED_STATE_OTA_SERIAL);
                    state = SERIAL_STATE_OTA_HEADER;
                    ota_count = 0;
                    break;
                }
                if (b == 0x02) {
                    log_dual_printf("[SERIE] OTA 0x02 = Mesh (ROOT puis diffusion, LED bleu). Attente en-tete 38 octets...\r\n");
                    s_ota_serial_receiving = true;
                    ota_tree_set_uart_mode(0x02);
                    ota_tree_init();
                    led_manager_set_state(LED_STATE_OTA_MESH);
                    state = SERIAL_STATE_OTA_HEADER;
                    ota_count = 0;
                    break;
                }
            }
            
            // Si on a changé d'état, on continue la boucle principale sans délai
            if (state != SERIAL_STATE_IDLE) continue;
            
            // Sinon, on yield pour laisser la main aux autres tâches (WDT)
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (state == SERIAL_STATE_OTA_HEADER) {
            while (Serial.available() > 0 && ota_count < OTA_ADV_PAYLOAD_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_ADV_PAYLOAD_SIZE) {
                // Parsing Header
                ota_total_size = (uint32_t)ota_buf[0] | ((uint32_t)ota_buf[1] << 8) | ((uint32_t)ota_buf[2] << 16) | ((uint32_t)ota_buf[3] << 24);
                ota_total_chunks = (uint16_t)ota_buf[4] | ((uint16_t)ota_buf[5] << 8);
                memcpy(ota_md5, ota_buf + 6, 32);
                ota_md5[32] = '\0';
                
                char buf[100];
                snprintf(buf, sizeof(buf), "[SERIE] Header OK: size=%lu, chunks=%u. MD5=%.8s...", (unsigned long)ota_total_size, (unsigned)ota_total_chunks, ota_md5);
                log_dual_println(buf);
                
                ota_chunk_idx = 0;
                state = SERIAL_STATE_OTA_CHUNK;
                ota_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_CHUNK) {
            while (Serial.available() > 0 && ota_count < OTA_CHUNK_DATA_SIZE) {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_CHUNK_DATA_SIZE) {
                // Envoyer le chunk au manager OTA Tree
                uint32_t offset = ota_chunk_idx * OTA_CHUNK_DATA_SIZE;
                ota_tree_on_uart_chunk(offset, ota_buf, OTA_CHUNK_DATA_SIZE, ota_total_size, ota_md5);
                
                ota_chunk_idx++;
                ota_count = 0;
                
                if (ota_chunk_idx % 50 == 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[SERIE] Chunk %u/%u recu.", (unsigned)ota_chunk_idx, (unsigned)ota_total_chunks);
                    log_dual_println(buf);
                }

                if (ota_chunk_idx >= ota_total_chunks) {
                    log_dual_println("[SERIE] OTA UART terminee.");
                    s_ota_serial_receiving = false;
                    state = SERIAL_STATE_IDLE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
