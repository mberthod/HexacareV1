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

#include "mesh/serial_gateway.h"
#include "mesh/routing_manager.h"
#include "mesh/mesh_tree_protocol.h"
#include "OTA/official_ota_manager.h"
#include "config/config.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
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
struct TopoNode
{
    uint16_t id;
    uint16_t parentId;
    uint8_t layer;
    uint32_t lastSeen;
};
static TopoNode s_topo[TOPO_MAX];
static int s_topo_n = 0;
static TickType_t s_last_topology_tick = 0;

#define OTA_CHUNK_PREFIX "OTA_CHUNK:"
#define OTA_HEX_LEN 400

/** Plage valide pour la taille firmware OTA (octets). */
#define OTA_SIZE_MIN  (10000UL)
#define OTA_SIZE_MAX  (16UL * 1024UL * 1024UL)
/** Magic byte en tête d'une image ESP32 (.bin). */
#define ESP_APP_IMAGE_MAGIC_BYTE  0xE9

/** Vide le buffer RX série pour resynchroniser après une erreur OTA. */
static void serial_ota_flush_rx(void)
{
    while (Serial.available() > 0)
        (void)Serial.read();
}

// Mode binaire : 0x02 = OTA Mesh (ROOT diffuse). Sync = 3 octets 0x02 pour éviter faux déclenchements (0x02 dans les chunks).
#define OTA_SERIAL_MODE_SERIAL 0x01
#define OTA_SERIAL_MODE_MESH 0x02
#define OTA_SYNC_BYTE 0x02
#define OTA_SYNC_COUNT 3

enum SerialGatewayState
{
    SERIAL_STATE_IDLE = 0,
    SERIAL_STATE_OTA_HEADER,
    SERIAL_STATE_OTA_CHUNK
};

static bool s_serial_gateway_init_done = false;
static uint32_t s_msg_id_seq = 0;
/** True pendant la réception OTA Série (0x01 + header + chunks) : le mesh ne doit pas injecter OTA dans ota_mesh. */
static volatile bool s_ota_serial_receiving = false;
/** Mode OTA en cours : 0x01 = ROOT seul (locale, très rapide), 0x02 = Mesh (propagation). */
static uint8_t s_ota_uart_mode = 0;

/** Handles des tâches à suspendre pendant OTA locale (0x01) pour dédier le CPU/série à la réception. */
static TaskHandle_t s_task_routing = nullptr;
static TaskHandle_t s_task_ota_tree = nullptr;
static TaskHandle_t s_task_data_tx = nullptr;
static TaskHandle_t s_task_sensor = nullptr;

static void serial_gateway_suspend_tasks_for_ota_local(void);
static void serial_gateway_suspend_tasks_for_ota_mesh(void);

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
int serial_gateway_is_ota_serial_receiving(void)
{
    return s_ota_serial_receiving ? 1 : 0;
}

/**
 * @brief Enregistre les tâches à suspendre pendant OTA locale (0x01).
 */
void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t routing_handle, TaskHandle_t ota_handle, TaskHandle_t data_tx_handle, TaskHandle_t sensor_handle)
{
    s_task_routing = routing_handle;
    s_task_ota_tree = ota_handle;
    s_task_data_tx = data_tx_handle;
    s_task_sensor = sensor_handle;
}

/** Suspend toutes les tâches enregistrées (0x01 et 0x02). */
static void serial_gateway_suspend_tasks_for_ota_local(void)
{
    if (s_task_routing)
        vTaskSuspend(s_task_routing);
    if (s_task_ota_tree)
        vTaskSuspend(s_task_ota_tree);
    if (s_task_data_tx)
        vTaskSuspend(s_task_data_tx);
    if (s_task_sensor)
        vTaskSuspend(s_task_sensor);
}

/** Suspend les tâches annexes pour OTA mesh, mais laisse ota_tree_task active (retries ACK). */
static void serial_gateway_suspend_tasks_for_ota_mesh(void)
{
    if (s_task_routing)
        vTaskSuspend(s_task_routing);
    if (s_task_data_tx)
        vTaskSuspend(s_task_data_tx);
    if (s_task_sensor)
        vTaskSuspend(s_task_sensor);
}

void serial_gateway_resume_tasks_after_ota_mesh(void)
{
    if (s_task_routing)
        vTaskResume(s_task_routing);
    if (s_task_data_tx)
        vTaskResume(s_task_data_tx);
    if (s_task_sensor)
        vTaskResume(s_task_sensor);
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
static int hex_to_binary(const char *hex, size_t hex_len, uint8_t *out, size_t out_size)
{
    if (hex_len % 2 != 0 || out_size < hex_len / 2)
        return -1;
    for (size_t i = 0; i < hex_len && i / 2 < out_size; i += 2)
    {
        char c1 = hex[i], c2 = hex[i + 1];
        int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10)
                                                     : (c1 >= 'a' && c1 <= 'f')   ? (c1 - 'a' + 10)
                                                                                  : -1;
        int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10)
                                                     : (c2 >= 'a' && c2 <= 'f')   ? (c2 - 'a' + 10)
                                                                                  : -1;
        if (v1 < 0 || v2 < 0)
            return -1;
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
static void topology_add_node(uint16_t id, uint16_t parentId, uint8_t layer)
{
    uint32_t now = xTaskGetTickCount();
    for (int i = 0; i < s_topo_n; i++)
    {
        if (s_topo[i].id == id)
        {
            s_topo[i].parentId = parentId;
            s_topo[i].layer = layer;
            s_topo[i].lastSeen = now;
            return;
        }
    }
    if (s_topo_n < TOPO_MAX)
    {
        s_topo[s_topo_n].id = id;
        s_topo[s_topo_n].parentId = parentId;
        s_topo[s_topo_n].layer = layer;
        s_topo[s_topo_n].lastSeen = now;
        s_topo_n++;
    }
}
/**
 * @brief Ajoute ou met à jour un nœud dans le cache de la topologie mesh.
 *
 * Cette fonction enregistre un nœud du réseau mesh dans le cache interne du gateway,
 * utilisé pour le suivi en temps réel de la topologie du mesh (relations parent/enfant, numéro de couche, dernier "seen").
 *
 * - Si le nœud (identifié par son ID) existe déjà dans le cache, sa parentId, layer et lastSeen sont mis à jour.
 * - Sinon, s'il reste de la place, un nouvel enregistrement est ajouté dans la table (s_topo).
 *
 * Le cache de topologie permet&nbsp;:
 *   - à la gateway de connaitre la structure actuelle du réseau ;
 *   - d’exporter des messages de type [TOPOLOGY] sur la sortie série/JSON ;
 *   - d’afficher des visualisations/réseaux sur le logiciel PC.
 *
 * @param id        Identifiant court du nœud (uint16_t, généralement extrait du MAC via routing).
 * @param parentId  Identifiant du parent (uint16_t) du nœud. 0 si racine (ROOT).
 * @param layer     Profondeur dans l’arbre mesh (0 = ROOT, 1 = enfants directs du ROOT, etc.)
 *
 * @remarks
 *  - La taille maximale du cache est fixée à TOPO_MAX.
 *  - La valeur de lastSeen utilise xTaskGetTickCount() (ticks FreeRTOS, monotones).
 *  - Si la table est pleine, tout nouvel ajout est ignoré.
 *
 * @see serial_gateway_send_topology_json()
 * @see TOPO_MAX
 * @see s_topo
 * @see s_topo_n
 */

void serial_gateway_send_data_json(const void *frame)
{
    /* Pendant OTA locale (0x01), ne rien envoyer sur Serial pour ne pas perturber réception/écriture flash */
    if (s_ota_serial_receiving)
        return;

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
    /* Version firmware : priorité à la trame (chaque nœud envoie sa version pour OTA), sinon paramètre (compat ancien firmware) */
    doc["fw_ver"] = (unsigned)f->fw_ver;

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0)
    {
        char out[560];
        int out_len = snprintf(out, sizeof(out), "[MESH] %s\r\n", buf);
        if (out_len > 0)
        {
            size_t write_len = (out_len < (int)sizeof(out)) ? (size_t)out_len : (sizeof(out) - 1);
            log_dual_write((const uint8_t *)out, write_len);
        }
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
static void process_line(char *line, size_t len)
{
    if (len < sizeof(OTA_CHUNK_PREFIX) - 1)
        return;
    if (memcmp(line, OTA_CHUNK_PREFIX, sizeof(OTA_CHUNK_PREFIX) - 1) != 0)
        return;
    char *p = line + sizeof(OTA_CHUNK_PREFIX) - 1;
    unsigned long idx = strtoul(p, &p, 10);
    if (*p != ':')
        return;
    p++;
    unsigned long total = strtoul(p, &p, 10);
    if (*p != ':')
        return;
    p++;
    if (idx > 0xFFFF || total > 0xFFFF || total == 0)
        return;
    size_t hex_len = strlen(p);
    while (hex_len > 0 && (p[hex_len - 1] == '\r' || p[hex_len - 1] == '\n'))
        hex_len--;
    if (hex_len != OTA_HEX_LEN)
        return;

    uint8_t data[OTA_CHUNK_DATA_SIZE];
    int bin_len = hex_to_binary(p, hex_len, data, sizeof(data));
    if (bin_len != (int)OTA_CHUNK_DATA_SIZE)
        return;

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
int serial_gateway_init(void)
{
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
void serial_gateway_task(void *pv)
{
    (void)pv;
    /* Éviter d'envoyer [TOPOLOGY] dès la 1re boucle (risque stack + WDT) */
    s_last_topology_tick = xTaskGetTickCount();
    log_dual_println("[TASK] serial_gateway running (Core 0)");

    static char line_buf[600];
    size_t pos = 0;

    enum SerialGatewayState state = SERIAL_STATE_IDLE;
    static uint8_t ota_buf[256];
    size_t ota_count = 0;

    // Métadonnées OTA (mode unique 0x02 : UART -> flash ROOT -> distribution espnow_ota)
    uint32_t ota_total_size = 0;
    uint16_t ota_total_chunks = 0;
    char ota_md5[33] = {0};
    uint16_t ota_chunk_idx = 0;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = nullptr;
    int ota_sync_count = 0; /* 0..OTA_SYNC_COUNT : nombre de 0x02 consécutifs reçus en IDLE */

    for (;;)
    {
        if (state == SERIAL_STATE_IDLE)
        {
            /* Envoi périodique [TOPOLOGY] pour l’interface Python (architecture réseau) */
            TickType_t now_tick = xTaskGetTickCount();
            if ((now_tick - s_last_topology_tick) >= pdMS_TO_TICKS(TOPO_INTERVAL_MS))
            {
                s_last_topology_tick = now_tick;
                StaticJsonDocument<1024> topo_doc;
                topo_doc["root"] = (unsigned)routing_get_my_id();
                JsonArray arr = topo_doc.createNestedArray("nodes");
                JsonObject root_node = arr.createNestedObject();
                root_node["id"] = (unsigned)routing_get_my_id();
                root_node["parentId"] = (uint32_t)0xFFFF; /* null = pas de parent */
                root_node["layer"] = 0;
                for (int i = 0; i < s_topo_n; i++)
                {
                    JsonObject n = arr.createNestedObject();
                    n["id"] = (unsigned)s_topo[i].id;
                    n["parentId"] = (unsigned)s_topo[i].parentId;
                    n["layer"] = (unsigned)s_topo[i].layer;
                }
                char topo_buf[1024];
                size_t topo_len = serializeJson(topo_doc, topo_buf, sizeof(topo_buf));
                if (topo_len > 0)
                {
                    char out[1088];
                    int out_len = snprintf(out, sizeof(out), "[TOPOLOGY]%s\r\n", topo_buf);
                    if (out_len > 0)
                    {
                        size_t write_len = (out_len < (int)sizeof(out)) ? (size_t)out_len : (sizeof(out) - 1);
                        log_dual_write((const uint8_t *)out, write_len);
                    }
                    vTaskDelay(pdMS_TO_TICKS(5)); /* Yield pour éviter WDT */
                }
            }

            // Sync OTA : exiger 3 octets 0x02 consécutifs pour éviter de prendre un 0x02 dans les chunks pour un démarrage.
            int bytesRead = 0;
            while (Serial.available() > 0 && bytesRead < 128)
            {
                int b = Serial.read();
                bytesRead++;
                if (b == OTA_SYNC_BYTE)
                {
                    ota_sync_count++;
                    if (ota_sync_count >= OTA_SYNC_COUNT)
                    {
                        ota_sync_count = 0;
                        s_ota_uart_mode = OTA_SERIAL_MODE_MESH;
                        log_dual_printf("[SERIE] OTA sync 3x0x02 OK. Attente en-tete 38 octets...\r\n");
                        s_ota_serial_receiving = true;
                        serial_gateway_suspend_tasks_for_ota_mesh();
                        led_manager_set_state(LED_STATE_OTA_MESH_ROOT);
                        state = SERIAL_STATE_OTA_HEADER;
                        ota_count = 0;
                        break;
                    }
                }
                else
                {
                    ota_sync_count = 0;
                    if (b == 0x01)
                        log_dual_printf("[SERIE] OTA 0x01 ignore (mode unique 0x02).\r\n");
                }
            }

            // Si on a changé d'état, on continue la boucle principale sans délai
            if (state != SERIAL_STATE_IDLE)
                continue;

            // Sinon, on yield pour laisser la main aux autres tâches (WDT)
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (state == SERIAL_STATE_OTA_HEADER)
        {
            while (Serial.available() > 0 && ota_count < OTA_ADV_PAYLOAD_SIZE)
            {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_ADV_PAYLOAD_SIZE)
            {
                // Parsing Header
                ota_total_size = (uint32_t)ota_buf[0] | ((uint32_t)ota_buf[1] << 8) | ((uint32_t)ota_buf[2] << 16) | ((uint32_t)ota_buf[3] << 24);
                ota_total_chunks = (uint16_t)ota_buf[4] | ((uint16_t)ota_buf[5] << 8);
                memcpy(ota_md5, ota_buf + 6, 32);
                ota_md5[32] = '\0';

                char buf[100];
                snprintf(buf, sizeof(buf), "[SERIE] Header OK: size=%lu, chunks=%u. MD5=%.8s...", (unsigned long)ota_total_size, (unsigned)ota_total_chunks, ota_md5);
                log_dual_println(buf);

                /* Validation : rejeter tailles incohérentes (données parasites ou désync) */
                uint32_t expected_chunks = (ota_total_size + OTA_CHUNK_DATA_SIZE - 1) / OTA_CHUNK_DATA_SIZE;
                if (ota_total_size < OTA_SIZE_MIN || ota_total_size > OTA_SIZE_MAX ||
                    ota_total_chunks == 0 || ota_total_chunks > 100000 ||
                    ota_total_chunks != expected_chunks)
                {
                    log_dual_printf("[SERIE] ERREUR: en-tete invalide (size=%lu chunks=%u, attendu chunks~%lu). Flush RX.\r\n",
                                   (unsigned long)ota_total_size, (unsigned)ota_total_chunks, (unsigned long)expected_chunks);
                    serial_ota_flush_rx();
                    serial_gateway_resume_tasks_after_ota_mesh();
                    s_ota_serial_receiving = false;
                    ota_sync_count = 0;
                    state = SERIAL_STATE_IDLE;
                    ota_count = 0;
                    continue;
                }

                ota_partition = esp_ota_get_next_update_partition(nullptr);
                if (ota_partition == nullptr)
                {
                    log_dual_println("[SERIE] ERREUR: pas de partition OTA update.");
                    serial_ota_flush_rx();
                    serial_gateway_resume_tasks_after_ota_mesh();
                    s_ota_serial_receiving = false;
                    ota_sync_count = 0;
                    state = SERIAL_STATE_IDLE;
                    ota_count = 0;
                    continue;
                }
                esp_err_t err = esp_ota_begin(ota_partition, ota_total_size, &ota_handle);
                if (err != ESP_OK)
                {
                    log_dual_printf("[SERIE] ERREUR esp_ota_begin: %s\r\n", esp_err_to_name(err));
                    serial_ota_flush_rx();
                    serial_gateway_resume_tasks_after_ota_mesh();
                    s_ota_serial_receiving = false;
                    ota_sync_count = 0;
                    state = SERIAL_STATE_IDLE;
                    ota_count = 0;
                    continue;
                }
                log_dual_println("[SERIE] OTA flash begun, attente chunks. Envoi OTA_CHUNK_OK par chunk.");

                ota_chunk_idx = 0;
                state = SERIAL_STATE_OTA_CHUNK;
                ota_count = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (state == SERIAL_STATE_OTA_CHUNK)
        {
            while (Serial.available() > 0 && ota_count < OTA_CHUNK_DATA_SIZE)
            {
                ota_buf[ota_count++] = (uint8_t)Serial.read();
            }
            if (ota_count >= OTA_CHUNK_DATA_SIZE)
            {
                uint32_t offset = ota_chunk_idx * OTA_CHUNK_DATA_SIZE;
                uint16_t chunk_len_effective = OTA_CHUNK_DATA_SIZE;
                if (offset >= ota_total_size)
                {
                    chunk_len_effective = 0;
                }
                else
                {
                    uint32_t remaining = ota_total_size - offset;
                    if (remaining < (uint32_t)OTA_CHUNK_DATA_SIZE)
                        chunk_len_effective = (uint16_t)remaining;
                }

                if (chunk_len_effective > 0 && ota_handle != 0)
                {
                    /* Premier chunk : l'image ESP32 doit commencer par 0xE9 (magic). */
                    if (ota_chunk_idx == 0 && ota_buf[0] != (uint8_t)ESP_APP_IMAGE_MAGIC_BYTE)
                    {
                        log_dual_printf("[SERIE] ERREUR: premier chunk magic invalide (0x%02X, attendu 0xE9). Desync?\r\n", (unsigned)ota_buf[0]);
                        esp_ota_abort(ota_handle);
                        ota_handle = 0;
                        serial_ota_flush_rx();
                        serial_gateway_resume_tasks_after_ota_mesh();
                        s_ota_serial_receiving = false;
                        ota_sync_count = 0;
                        state = SERIAL_STATE_IDLE;
                        ota_count = 0;
                        continue;
                    }
                    esp_err_t err = esp_ota_write(ota_handle, ota_buf, chunk_len_effective);
                    if (err != ESP_OK)
                    {
                        log_dual_printf("[SERIE] ERREUR esp_ota_write chunk %u: %s\r\n", (unsigned)ota_chunk_idx, esp_err_to_name(err));
                        esp_ota_abort(ota_handle);
                        ota_handle = 0;
                        serial_ota_flush_rx();
                        serial_gateway_resume_tasks_after_ota_mesh();
                        s_ota_serial_receiving = false;
                        ota_sync_count = 0;
                        state = SERIAL_STATE_IDLE;
                        ota_count = 0;
                        continue;
                    }
                }
                /* ACK UART pour le script Python (OTA_CHUNK_OK <idx>) */
                {
                    char ack[48];
                    int n = snprintf(ack, sizeof(ack), "OTA_CHUNK_OK %u\r\n", (unsigned)ota_chunk_idx);
                    if (n > 0)
                        log_dual_write((const uint8_t *)ack, (size_t)n);
                }
                ota_chunk_idx++;
                ota_count = 0;
                if (ota_chunk_idx % 200 == 0 || ota_chunk_idx >= ota_total_chunks)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[SERIE] Chunk %u/%u recu (len_effectif=%u).",
                             (unsigned)ota_chunk_idx, (unsigned)ota_total_chunks, (unsigned)chunk_len_effective);
                    log_dual_println(buf);
                }

                if (ota_chunk_idx >= ota_total_chunks)
                {
                    if (ota_handle != 0)
                    {
                        esp_err_t err = esp_ota_end(ota_handle);
                        ota_handle = 0;
                        if (err != ESP_OK)
                        {
                            log_dual_printf("[SERIE] ERREUR esp_ota_end: %s\r\n", esp_err_to_name(err));
                            serial_gateway_resume_tasks_after_ota_mesh();
                            s_ota_serial_receiving = false;
                            state = SERIAL_STATE_IDLE;
                            continue;
                        }
                        log_dual_println("[SERIE] OTA UART ecriture flash terminee. Lancement distribution ESP-NOW...");
                        int dist_ok = start_ota_initiator_distribution((size_t)ota_total_size, nullptr);
                        if (dist_ok && ota_partition != nullptr)
                        {
                            err = esp_ota_set_boot_partition(ota_partition);
                            if (err == ESP_OK)
                            {
                                log_dual_println("[SERIE] Partition boot definie. Redémarrage dans 2s...");
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                esp_restart();
                            }
                            else
                                log_dual_printf("[SERIE] ERREUR esp_ota_set_boot_partition: %s\r\n", esp_err_to_name(err));
                        }
                        else
                            log_dual_println("[SERIE] Distribution ESP-NOW echouee ou aucun repondeur.");
                    }
                    }
                    serial_gateway_resume_tasks_after_ota_mesh();
                    s_ota_serial_receiving = false;
                    ota_sync_count = 0;
                    state = SERIAL_STATE_IDLE;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

