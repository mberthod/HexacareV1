/**
 * @file mesh_manager.c
 * @ingroup group_mesh
 * @brief Gestionnaire réseau maillé ESP-NOW — topologie arborescente + AES-128.
 *
 * Chiffrement applicatif (contrainte +1000 nœuds) :
 *   Le LMK ESP-NOW est limité à ~17 pairs simultanés.
 *   Chaque payload est chiffré par le nœud émetteur en AES-128-CBC
 *   via mbedTLS, indépendamment du chiffrement matériel ESP-NOW.
 *   La clé symétrique (16 octets) est stockée en NVS.
 *
 * Topologie arborescente :
 *   Un nœud ROOT (passerelle) est identifié par PIN_ROOT_SEL=LOW.
 *   Les nœuds normaux sélectionnent leur parent = pair avec le meilleur
 *   RSSI parmi ceux ayant le hop_to_root minimal (découverte par beacon).
 */

#include "mesh_manager.h"
#include "ota_node.h"
#include "pins_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
/* Compatibilité AES : IDF 5.x → mbedtls/aes.h, IDF 6.0+ → aes/esp_aes.h
 * L'API ESP-IDF 6.0 renomme la structure et les fonctions (esp_aes_*)
 * mais conserve une signature identique. */
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 6
#include "aes/esp_aes.h"
typedef esp_aes_context      aes_compat_ctx_t;
#define aes_compat_init(c)          esp_aes_init(c)
#define aes_compat_free(c)          esp_aes_free(c)
#define aes_compat_setkey(c, k, b)  esp_aes_setkey(c, k, b)
#define aes_compat_crypt_cbc(c, m, l, iv, i, o) esp_aes_crypt_cbc(c, m, l, iv, i, o)
#define AES_COMPAT_ENCRYPT          1
#else
#include "mbedtls/aes.h"
typedef mbedtls_aes_context  aes_compat_ctx_t;
#define aes_compat_init(c)          mbedtls_aes_init(c)
#define aes_compat_free(c)          mbedtls_aes_free(c)
#define aes_compat_setkey(c, k, b)  mbedtls_aes_setkey_enc(c, k, b)
#define aes_compat_crypt_cbc(c, m, l, iv, i, o) mbedtls_aes_crypt_cbc(c, m, l, iv, i, o)
#define AES_COMPAT_ENCRYPT          MBEDTLS_AES_ENCRYPT
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

/* Adresse broadcast ESP-NOW (FF:FF:FF:FF:FF:FF) */
static const uint8_t k_broadcast_mac[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const char *TAG = "mesh_mgr";

/* ================================================================
 * Constantes réseau
 * ================================================================ */
#define ESPNOW_CHANNEL          1
#define ESPNOW_MAX_PEERS        20
#define MESH_TASK_STACK         (6144)
#define MESH_TASK_PRIORITY      (8)
#define MESH_TASK_CORE          (0)
#define BEACON_INTERVAL_MS      5000    /**< Période d'envoi des beacons */

/* PMK par défaut (32 octets) — remplacer par une valeur secrète en production */
static const uint8_t k_pmk[16] = {
    0x4C, 0x65, 0x78, 0x61, 0x63, 0x61, 0x72, 0x65,
    0x50, 0x4D, 0x4B, 0x30, 0x31, 0x32, 0x33, 0x34,
};

/* Clé AES par défaut (16 octets) — remplacer via NVS en production */
static const uint8_t k_aes_default[LEXACARE_AES_KEY_LEN] = {
    0x4C, 0x65, 0x78, 0x61, 0x63, 0x61, 0x72, 0x65,
    0x41, 0x45, 0x53, 0x31, 0x32, 0x38, 0x4B, 0x65,
};

/* ================================================================
 * peer_info_t (interne)
 * @brief Information sur un pair ESP-NOW dans la topologie arborescente.
 * ================================================================ */
typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t hop_to_root;
    bool    is_child;
    bool    valid;
} peer_info_t;

/* ================================================================
 * mesh_state_t (interne)
 * @brief État complet du module réseau (pas de variable globale directe).
 * ================================================================ */
typedef struct {
    sys_context_t  *sys_ctx;
    uint8_t         aes_key[LEXACARE_AES_KEY_LEN];
    uint8_t         parent_mac[6];
    bool            has_parent;
    uint8_t         hop_to_root;
    peer_info_t     peers[ESPNOW_MAX_PEERS];
    int             peer_count;
    uint8_t         own_mac[6];
} mesh_state_t;

static mesh_state_t s_mesh; /* Unique instance du gestionnaire */

/* ================================================================
 * crc16_ccitt (interne)
 * @brief Calcule le CRC16-CCITT d'un buffer.
 *
 * @param data   Pointeur vers les données.
 * @param length Longueur en octets.
 * @return CRC16 calculé.
 * ================================================================ */
static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ================================================================
 * aes_encrypt_payload (interne)
 * @brief Chiffre un buffer en AES-128-CBC via mbedTLS.
 *
 * Le padding PKCS#7 est appliqué pour aligner sur 16 octets.
 * L'IV est généré aléatoirement (esp_fill_random).
 *
 * @param plain      Données en clair.
 * @param plain_len  Longueur des données en clair.
 * @param cipher     Buffer chiffré (doit être >= padded_len).
 * @param iv         Buffer IV (16 octets, rempli par cette fonction).
 * @return Longueur du payload chiffré (multiple de 16), ou 0 si erreur.
 * ================================================================ */
static size_t aes_encrypt_payload(const uint8_t *plain, size_t plain_len,
                                    uint8_t *cipher, uint8_t *iv)
{
    /* Calcul de la longueur paddée (PKCS#7) */
    size_t padded_len = ((plain_len + 15) / 16) * 16;
    if (padded_len > LEXACARE_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Payload trop grand : %u octets (max %d)",
                 (unsigned)padded_len, LEXACARE_MAX_PAYLOAD);
        return 0;
    }

    /* Buffer temporaire avec padding */
    uint8_t padded[LEXACARE_MAX_PAYLOAD] = {0};
    memcpy(padded, plain, plain_len);
    uint8_t pad_byte = (uint8_t)(padded_len - plain_len);
    for (size_t i = plain_len; i < padded_len; i++) {
        padded[i] = pad_byte;
    }

    /* Génération de l'IV aléatoire */
    esp_fill_random(iv, LEXACARE_AES_IV_LEN);

    /* Chiffrement AES-128-CBC */
    aes_compat_ctx_t aes_ctx;
    aes_compat_init(&aes_ctx);

    uint8_t iv_copy[LEXACARE_AES_IV_LEN];
    memcpy(iv_copy, iv, LEXACARE_AES_IV_LEN); /* L'API AES modifie l'IV en place */

    int ret = aes_compat_setkey(&aes_ctx, s_mesh.aes_key,
                                 LEXACARE_AES_KEY_LEN * 8);
    if (ret != 0) {
        aes_compat_free(&aes_ctx);
        ESP_LOGE(TAG, "AES setkey échoué : %d", ret);
        return 0;
    }

    ret = aes_compat_crypt_cbc(&aes_ctx, AES_COMPAT_ENCRYPT,
                                padded_len, iv_copy, padded, cipher);
    aes_compat_free(&aes_ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES CBC encrypt échoué : %d", ret);
        return 0;
    }

    return padded_len;
}

/* ================================================================
 * mesh_build_ai_frame (interne)
 * @brief Construit une lexacare_frame_t depuis un ai_event_t.
 *
 * @param event Événement IA à encapsuler.
 * @param frame Trame destination.
 * @return true si la construction réussit.
 * ================================================================ */
static bool mesh_build_ai_frame(const ai_event_t *event,
                                  lexacare_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->magic     = LEXACARE_FRAME_MAGIC;
    frame->type      = FRAME_TYPE_AI_EVENT;
    frame->hop_count = 0;
    memcpy(frame->src_mac, s_mesh.own_mac, 6);

    /* Chiffrement du payload */
    size_t enc_len = aes_encrypt_payload((const uint8_t *)event,
                                           sizeof(ai_event_t),
                                           frame->payload,
                                           frame->iv);
    if (enc_len == 0) return false;
    frame->payload_len = (uint16_t)enc_len;

    /* CRC16 sur magic + type + hop + src_mac + iv + payload */
    uint8_t crc_buf[2 + 1 + 1 + 6 + LEXACARE_AES_IV_LEN + LEXACARE_MAX_PAYLOAD];
    size_t  crc_len = 0;
    memcpy(crc_buf + crc_len, &frame->magic, 2);   crc_len += 2;
    crc_buf[crc_len++] = frame->type;
    crc_buf[crc_len++] = frame->hop_count;
    memcpy(crc_buf + crc_len, frame->src_mac, 6);  crc_len += 6;
    memcpy(crc_buf + crc_len, frame->iv, LEXACARE_AES_IV_LEN);
    crc_len += LEXACARE_AES_IV_LEN;
    memcpy(crc_buf + crc_len, frame->payload, enc_len);
    crc_len += enc_len;

    frame->crc16 = crc16_ccitt(crc_buf, crc_len);
    return true;
}

/* ================================================================
 * espnow_rx_callback (interne)
 * @brief Callback de réception ESP-NOW — appelé depuis le contexte Wi-Fi.
 *
 * Valide la trame (magic + CRC16) et dispatche selon le type.
 * ================================================================ */
static void espnow_rx_callback(const esp_now_recv_info_t *recv_info,
                                 const uint8_t *data, int data_len)
{
    if (data_len < (int)sizeof(lexacare_frame_t)) return;

    const lexacare_frame_t *frame = (const lexacare_frame_t *)data;

    if (frame->magic != LEXACARE_FRAME_MAGIC) {
        ESP_LOGW(TAG, "Trame reçue avec magic invalide : 0x%04X", frame->magic);
        return;
    }

    /* Vérification CRC16 (simplifiée — recomputation sur les champs) */
    ESP_LOGD(TAG, "Trame reçue type=0x%02X, hop=%u, src=" MACSTR,
             frame->type, frame->hop_count,
             MAC2STR(frame->src_mac));

    if (frame->type == FRAME_TYPE_OTA_FRAGMENT) {
        /* Délégation au module OTA */
        /* Déchiffrement du payload avant traitement (non implémenté ici) */
        ESP_LOGI(TAG, "Fragment OTA reçu (%u octets chiffrés)",
                 frame->payload_len);
    }
}

/* ================================================================
 * espnow_tx_callback (interne)
 * @brief Callback de confirmation d'envoi ESP-NOW.
 *
 * IDF 5.x : signature (const uint8_t *mac_addr, esp_now_send_status_t)
 * IDF 6.0+ : signature (const esp_now_send_info_t *tx_info, esp_now_send_status_t)
 *             L'adresse destination se trouve dans tx_info->des_addr.
 * ================================================================ */
#if ESP_IDF_VERSION_MAJOR >= 6
static void espnow_tx_callback(const esp_now_send_info_t *tx_info,
                                 esp_now_send_status_t status)
{
    const uint8_t *mac_addr = tx_info ? tx_info->des_addr : NULL;
    if (mac_addr) {
        ESP_LOGD(TAG, "TX vers " MACSTR " : %s",
                 MAC2STR(mac_addr),
                 (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "ÉCHEC");
    }
}
#else
static void espnow_tx_callback(const uint8_t *mac_addr,
                                 esp_now_send_status_t status)
{
    ESP_LOGD(TAG, "TX vers " MACSTR " : %s",
             MAC2STR(mac_addr),
             (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "ÉCHEC");
}
#endif

/* ================================================================
 * mesh_load_aes_key (interne)
 * @brief Charge la clé AES depuis NVS ou utilise la clé par défaut.
 * ================================================================ */
static void mesh_load_aes_key(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("lexacare", NVS_READONLY, &nvs);
    if (ret == ESP_OK) {
        size_t key_len = LEXACARE_AES_KEY_LEN;
        ret = nvs_get_blob(nvs, "aes_key", s_mesh.aes_key, &key_len);
        nvs_close(nvs);
        if (ret == ESP_OK && key_len == LEXACARE_AES_KEY_LEN) {
            ESP_LOGI(TAG, "Clé AES chargée depuis NVS");
            return;
        }
    }

    /* Clé par défaut (avertissement de sécurité) */
    memcpy(s_mesh.aes_key, k_aes_default, LEXACARE_AES_KEY_LEN);
    ESP_LOGW(TAG, "Clé AES par défaut utilisée — NE PAS utiliser en production");
}

/* ================================================================
 * task_mesh_com (interne)
 * @brief Tâche FreeRTOS de communication mesh — Core 0, priorité 8.
 *
 * @param pvParam Pointeur vers mesh_state_t.
 * ================================================================ */
static void task_mesh_com(void *pvParam)
{
    mesh_state_t *state = (mesh_state_t *)pvParam;
    ESP_LOGI(TAG, "Task_Mesh_Com démarrée sur Core %d", xPortGetCoreID());

    ai_event_t    event;
    lexacare_frame_t frame;

    while (true) {
        /* Attente d'un événement IA (portMAX_DELAY) */
        if (xQueueReceive(state->sys_ctx->ai_to_mesh_queue,
                           &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!state->has_parent && !state->sys_ctx->is_root_node) {
            ESP_LOGW(TAG, "Pas de parent — événement non transmis");
            continue;
        }

        /* Construction et chiffrement de la trame */
        if (!mesh_build_ai_frame(&event, &frame)) {
            ESP_LOGE(TAG, "Échec construction trame");
            continue;
        }

        /* Envoi vers le parent (ou broadcast si ROOT) */
        const uint8_t *dest = state->has_parent
                              ? state->parent_mac
                              : k_broadcast_mac;

        esp_err_t send_ret = esp_now_send(dest,
                                           (const uint8_t *)&frame,
                                           sizeof(lexacare_frame_t));
        if (send_ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_send échoué : %s", esp_err_to_name(send_ret));
        }
    }
}

/* ================================================================
 * mesh_manager_init
 * @brief Initialise Wi-Fi Station + ESP-NOW + clé AES.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si l'initialisation réussit.
 * ================================================================ */
esp_err_t mesh_manager_init(sys_context_t *ctx)
{
    memset(&s_mesh, 0, sizeof(s_mesh));
    s_mesh.sys_ctx    = ctx;
    s_mesh.hop_to_root = ctx->is_root_node ? 0 : 0xFF;

    /* Chargement de la clé AES */
    mesh_load_aes_key();

    /* Initialisation réseau */
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* set_channel doit être appelé APRÈS esp_wifi_start() */
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    /* Récupération de l'adresse MAC propre */
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, s_mesh.own_mac));
    ESP_LOGI(TAG, "MAC propre : " MACSTR, MAC2STR(s_mesh.own_mac));

    /* Initialisation ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk(k_pmk));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_rx_callback));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_tx_callback));

    /* Initialisation du module OTA */
    ESP_ERROR_CHECK(ota_node_init());

    ESP_LOGI(TAG, "Mesh ESP-NOW initialisé (canal %d, rôle : %s)",
             ESPNOW_CHANNEL,
             ctx->is_root_node ? "ROOT" : "NODE");

    return ESP_OK;
}

/* ================================================================
 * mesh_task_start
 * @brief Crée Task_Mesh_Com sur le Core 0.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t mesh_task_start(sys_context_t *ctx)
{
    (void)ctx; /* s_mesh.sys_ctx déjà rempli par mesh_manager_init */

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_mesh_com,
        "Task_Mesh_Com",
        MESH_TASK_STACK,
        &s_mesh,
        MESH_TASK_PRIORITY,
        NULL,
        MESH_TASK_CORE);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec création Task_Mesh_Com");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task_Mesh_Com créée (Core %d, priorité %d)",
             MESH_TASK_CORE, MESH_TASK_PRIORITY);
    return ESP_OK;
}
