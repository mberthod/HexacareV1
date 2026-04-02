/**
 * @file radar_driver.c
 * @ingroup group_sensor_acq
 * @brief Driver UART HLK-LD6002 — protocole TinyFrame, ring buffer UART.
 *
 * Protocole TinyFrame :
 *   [SOF=0x01][Type_H][Type_L][Longueur][Données...][EOF=0x04]
 *
 * Types reconnus :
 *   0x0A14 : Fréquence respiratoire (2 octets big-endian, bpm)
 *   0x0A15 : Fréquence cardiaque    (2 octets big-endian, bpm)
 *   0x0A16 : Distance de la cible   (2 octets big-endian, mm)
 *
 * Le driver UART (UART_NUM_2) est déjà installé par hw_diag_run().
 * radar_driver_init() se contente de réinitialiser l'état du parser.
 * radar_driver_poll() est appelé à chaque cycle de Task_Sensor_Acq.
 */

#include "radar_driver.h"
#include "pins_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "radar_driver";

/* ================================================================
 * Constantes du protocole
 * ================================================================ */
#define UART_PORT           UART_NUM_2
#define RX_BUF_SIZE         512
#define TF_SOF              0x01
#define TF_EOF              0x04
#define TF_HEADER_LEN       4    /**< SOF + Type(2) + Longueur(1) */
#define TF_FOOTER_LEN       1    /**< EOF */
#define TF_MIN_FRAME_LEN    (TF_HEADER_LEN + TF_FOOTER_LEN)
#define TF_MAX_PAYLOAD_LEN  64

#define TF_TYPE_BREATH      0x0A14
#define TF_TYPE_HEART       0x0A15
#define TF_TYPE_DISTANCE    0x0A16

/* ================================================================
 * État interne du parser (pas de variable globale — static local)
 * ================================================================ */
typedef struct {
    uint8_t  buf[RX_BUF_SIZE];
    size_t   len;
    bool     updated;
    radar_data_t last_data;
} radar_parser_t;

static radar_parser_t s_parser; /* Unique instance — module singulier */

/* ================================================================
 * parse_tinyframe (interne)
 * @brief Tente de décoder une trame TinyFrame dans le buffer.
 *
 * Si une trame valide est trouvée, met à jour s_parser.last_data.
 * Consomme les octets traités du buffer (glissement).
 *
 * @return true si une trame a été décodée.
 * ================================================================ */
static bool parse_tinyframe(void)
{
    if (s_parser.len < TF_MIN_FRAME_LEN) return false;

    /* Recherche du SOF */
    size_t sof_pos = 0;
    while (sof_pos < s_parser.len && s_parser.buf[sof_pos] != TF_SOF) {
        sof_pos++;
    }

    if (sof_pos > 0) {
        /* Supprimer les octets avant le SOF */
        s_parser.len -= sof_pos;
        memmove(s_parser.buf, s_parser.buf + sof_pos, s_parser.len);
    }

    if (s_parser.len < TF_MIN_FRAME_LEN) return false;

    /* Décodage du header */
    uint16_t type = ((uint16_t)s_parser.buf[1] << 8) | s_parser.buf[2];
    uint8_t  plen = s_parser.buf[3];

    /* Vérification de la longueur totale */
    size_t frame_len = TF_HEADER_LEN + plen + TF_FOOTER_LEN;
    if (frame_len > sizeof(s_parser.buf)) {
        /* Trame incohérente — resynchronisation */
        s_parser.len = 0;
        return false;
    }

    if (s_parser.len < frame_len) return false; /* Trame incomplète */

    /* Vérification EOF */
    if (s_parser.buf[TF_HEADER_LEN + plen] != TF_EOF) {
        /* EOF invalide : sauter d'un octet et réessayer */
        memmove(s_parser.buf, s_parser.buf + 1, --s_parser.len);
        return false;
    }

    /* Extraction des données selon le type */
    const uint8_t *payload = &s_parser.buf[TF_HEADER_LEN];
    bool decoded = false;

    switch (type) {
        case TF_TYPE_BREATH:
            if (plen >= 2) {
                s_parser.last_data.breath_rate_bpm =
                    (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
                s_parser.last_data.presence = (s_parser.last_data.breath_rate_bpm > 0);
                decoded = true;
                ESP_LOGD(TAG, "Respiration : %u bpm",
                         s_parser.last_data.breath_rate_bpm);
            }
            break;

        case TF_TYPE_HEART:
            if (plen >= 2) {
                s_parser.last_data.heart_rate_bpm =
                    (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
                decoded = true;
                ESP_LOGD(TAG, "Cardio : %u bpm",
                         s_parser.last_data.heart_rate_bpm);
            }
            break;

        case TF_TYPE_DISTANCE:
            if (plen >= 2) {
                s_parser.last_data.target_distance_mm =
                    (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
                decoded = true;
                ESP_LOGD(TAG, "Distance : %u mm",
                         s_parser.last_data.target_distance_mm);
            }
            break;

        default:
            ESP_LOGV(TAG, "Type TinyFrame inconnu : 0x%04X", type);
            break;
    }

    if (decoded) {
        s_parser.last_data.timestamp_us = esp_timer_get_time();
        s_parser.updated = true;
    }

    /* Consommer la trame du buffer */
    memmove(s_parser.buf,
            s_parser.buf + frame_len,
            s_parser.len - frame_len);
    s_parser.len -= frame_len;

    return decoded;
}

/* ================================================================
 * radar_driver_init
 * @brief Réinitialise l'état du parser TinyFrame.
 *
 * Le driver UART est déjà installé par hw_diag_run().
 *
 * @return ESP_OK toujours.
 * ================================================================ */
esp_err_t radar_driver_init(void)
{
    memset(&s_parser, 0, sizeof(s_parser));
    ESP_LOGI(TAG, "Parser TinyFrame LD6002 initialisé (UART%d, %d bauds)",
             UART_PORT, 1382400);
    return ESP_OK;
}

/* ================================================================
 * radar_driver_poll
 * @brief Lit les octets UART disponibles et tente de décoder une trame.
 *
 * Non bloquant : timeout = 0 (retourne immédiatement si rien à lire).
 * Appelé depuis la boucle de Task_Sensor_Acq.
 *
 * @param out Pointeur vers radar_data_t à mettre à jour si nouvelle trame.
 * @return true si de nouvelles données ont été décodées.
 * ================================================================ */
bool radar_driver_poll(radar_data_t *out)
{
    /* Lecture non bloquante des octets disponibles */
    size_t available = sizeof(s_parser.buf) - s_parser.len;
    if (available > 0) {
        int read = uart_read_bytes(UART_PORT,
                                    s_parser.buf + s_parser.len,
                                    available,
                                    0 /* timeout = 0 ms, non bloquant */);
        if (read > 0) {
            s_parser.len += (size_t)read;
        }
    }

    /* Tentative de décodage (peut trouver plusieurs trames) */
    bool found = false;
    while (parse_tinyframe()) {
        found = true;
    }

    if (found && out) {
        *out = s_parser.last_data;
        s_parser.updated = false;
    }

    return found;
}
