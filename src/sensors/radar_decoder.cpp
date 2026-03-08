/**
 * @file radar_decoder.cpp
 * @brief Parser UART pour le radar HLK-LD6002.
 * 
 * Ce module décode les flux de données provenant du radar HLK-LD6002 via UART.
 * Il supporte deux formats : le protocole binaire TinyFrame et le format ASCII.
 * Les données extraites incluent la présence, le rythme cardiaque et la respiration.
 */

#include "config/config.h"
#include "radar_decoder.h"
#include "config/pins_lexacare.h"
#include <HardwareSerial.h>
#include <Arduino.h>
#include "esp_log.h"

static const char* TAG = "RADAR";

#define RADAR_RX_BUF_SIZE 256               ///< Taille du buffer de réception UART
#define TF_SOF 0x01                         ///< Start Of Frame (TinyFrame)
#define TF_EOF 0x04                         ///< End Of Frame (TinyFrame)

static HardwareSerial *s_serial = nullptr;
static uint8_t s_rx_buf[RADAR_RX_BUF_SIZE];
static size_t s_rx_len = 0;
static vital_signs_t s_vitals;

/**
 * @brief Ajoute un octet au buffer circulaire de réception.
 * @param b Octet à ajouter.
 */
static void push_byte(uint8_t b) {
    if (s_rx_len < RADAR_RX_BUF_SIZE)
        s_rx_buf[s_rx_len++] = b;
    else {
        ESP_LOGV(TAG, "Buffer RX plein, décalage...");
        memmove(s_rx_buf, s_rx_buf + 1, RADAR_RX_BUF_SIZE - 1);
        s_rx_buf[RADAR_RX_BUF_SIZE - 1] = b;
    }
}

/**
 * @brief Décode une trame binaire TinyFrame.
 * 
 * Types supportés :
 * - 0x0A14 : Respiration
 * - 0x0A15 : Rythme cardiaque
 * - 0x0A16 : Distance de la cible
 */
static void parse_tinyframe(void) {
    if (s_rx_len < 6) return;
    if (s_rx_buf[0] != TF_SOF) {
        ESP_LOGV(TAG, "TinyFrame: SOF invalide");
        s_rx_len = 0;
        return;
    }
    uint16_t type = (uint16_t)s_rx_buf[1] << 8 | s_rx_buf[2];
    uint8_t plen = s_rx_buf[3];
    if (s_rx_len < (size_t)(4 + plen + 1)) return;
    if (s_rx_buf[4 + plen] != TF_EOF) {
        ESP_LOGV(TAG, "TinyFrame: EOF invalide");
        s_rx_len = 0;
        return;
    }
    uint8_t *p = &s_rx_buf[4];
    ESP_LOGD(TAG, "TinyFrame type 0x%04X reçue", type);
    switch (type) {
        case 0x0A14:
            if (plen >= 2) {
                s_vitals.breath_rate_bpm = (uint16_t)p[0] << 8 | p[1];
                s_vitals.presence = s_vitals.breath_rate_bpm > 0;
                ESP_LOGI(TAG, "Respiration: %d bpm", s_vitals.breath_rate_bpm);
            }
            break;
        case 0x0A15:
            if (plen >= 2) {
                s_vitals.heart_rate_bpm = (uint16_t)p[0] << 8 | p[1];
                ESP_LOGI(TAG, "Rythme Cardiaque: %d bpm", s_vitals.heart_rate_bpm);
            }
            break;
        case 0x0A16:
            if (plen >= 2) {
                s_vitals.target_distance_mm = (uint16_t)p[0] << 8 | p[1];
                ESP_LOGD(TAG, "Distance: %d mm", s_vitals.target_distance_mm);
            }
            break;
        default:
            ESP_LOGW(TAG, "TinyFrame: Type inconnu 0x%04X", type);
            break;
    }
    s_vitals.last_update_ms = millis();
    system_state_set_vitals(&s_vitals);
    memmove(s_rx_buf, s_rx_buf + 4 + plen + 1, s_rx_len - (4 + plen + 1));
    s_rx_len -= (4 + plen + 1);
}

/**
 * @brief Décode une ligne de texte ASCII.
 * Format attendu : "HR:72 BR:18 DIST:1200"
 */
static void parse_ascii_line(void) {
    s_rx_buf[s_rx_len] = '\0';
    char *s = (char *)s_rx_buf;
    ESP_LOGD(TAG, "Ligne ASCII reçue: %s", s);
    if (strstr(s, "HR:") != nullptr) {
        int hr = 0;
        sscanf(s, "HR:%d", &hr);
        if (hr > 0 && hr < 255) s_vitals.heart_rate_bpm = (uint16_t)hr;
    }
    if (strstr(s, "BR:") != nullptr) {
        int br = 0;
        sscanf(s, "BR:%d", &br);
        if (br >= 0 && br < 255) s_vitals.breath_rate_bpm = (uint16_t)br;
    }
    if (strstr(s, "DIST:") != nullptr) {
        int d = 0;
        sscanf(s, "DIST:%d", &d);
        if (d >= 0 && d < 5000) s_vitals.target_distance_mm = (uint16_t)d;
    }
    s_vitals.presence = s_vitals.heart_rate_bpm > 0 || s_vitals.breath_rate_bpm > 0;
    s_vitals.last_update_ms = millis();
    system_state_set_vitals(&s_vitals);
    s_rx_len = 0;
}

/**
 * @brief Initialise la liaison UART1 pour le radar.
 */
void radar_decoder_init(void) {
    ESP_LOGI(TAG, "Initialisation UART Radar...");
    memset(&s_vitals, 0, sizeof(s_vitals));
    s_rx_len = 0;
    s_serial = &Serial1;
    s_serial->begin(RADAR_UART_BAUD, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    s_serial->setRxBufferSize(RADAR_UART_RX_BUF);
    s_serial->setTxBufferSize(RADAR_UART_TX_BUF);
    ESP_LOGI(TAG, "UART Radar prêt à %d bauds", RADAR_UART_BAUD);
}

/**
 * @brief Lit et traite les octets disponibles sur l'UART.
 */
void radar_decoder_poll(void) {
    if (!s_serial) return;
    while (s_serial->available()) {
        uint8_t b = (uint8_t)s_serial->read();
        push_byte(b);
        if (b == '\n' || b == '\r') {
            if (s_rx_len > 4)
                parse_ascii_line();
            else
                s_rx_len = 0;
        } else if (b == TF_EOF && s_rx_len >= 5)
            parse_tinyframe();
    }
}

/**
 * @brief Récupère une copie des derniers signes vitaux.
 * @param out Pointeur vers la structure de destination.
 */
void radar_decoder_get_vitals(vital_signs_t *out) {
    if (out)
        system_state_get_vitals(out);
}
