/**
 * @file log_dual.cpp
 * @brief Le Journaliste (Système de Logs).
 *
 * @details
 * Ce module est responsable de noter tout ce qui se passe dans le système pour aider
 * les développeurs à comprendre les problèmes.
 *
 * Il écrit les messages à deux endroits en même temps ("Dual") :
 * 1. **Sur le port USB (Serial)** : Pour quand on branche le boîtier au PC.
 * 2. **Sur le port UART0 (RX/TX)** : Pour quand on utilise un adaptateur externe.
 *
 * C'est comme écrire dans un journal intime et envoyer un SMS en même temps.
 */

#include "config/config.h"
#include "config/pins_lexacare.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static HardwareSerial *s_uart0 = nullptr;
static bool s_uart0_ok = false;
/** Mutex récursif pour garantir l'intégrité des messages série (pas d'entrelacement). */
static SemaphoreHandle_t s_serial_write_mutex = nullptr;

static inline void dual_write_nolock(const uint8_t *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    Serial.write(buf, len);
    if (s_uart0_ok && s_uart0)
        s_uart0->write(buf, len);
}

static inline bool dual_lock_take(void)
{
    if (!s_serial_write_mutex)
        return false;
    /* Ne pas appeler les APIs blocantes FreeRTOS en contexte ISR. */
    if (xPortInIsrContext())
        return false;
    return (xSemaphoreTakeRecursive(s_serial_write_mutex, portMAX_DELAY) == pdTRUE);
}

static inline void dual_lock_give(bool locked)
{
    if (locked && s_serial_write_mutex)
        xSemaphoreGiveRecursive(s_serial_write_mutex);
}

// Hook pour rediriger les logs ESP_LOGx vers notre système dual
static int log_dual_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0)
    {
        if ((size_t)len >= sizeof(buf))
            len = sizeof(buf) - 1;
        log_dual_write((const uint8_t *)buf, len);
    }
    return len;
}

/**
 * @brief Initialisation du module Log Dual.
 *
 * @details
 * Prépare les deux canaux de sortie :
 * - USB (Serial) : Pour le debug sur PC.
 * - UART0 (Serial1) : Pour le debug avec un adaptateur externe.
 *
 * C'est comme allumer le micro et brancher les enceintes avant de commencer à parler.
 */
void log_dual_init(void)
{
    Serial.begin(LOG_DUAL_BAUD);
    /* Laisser le USB CDC (ESP32-S3) se stabiliser pour éviter perte de données RX */
    delay(500);
    if (!s_serial_write_mutex)
        s_serial_write_mutex = xSemaphoreCreateRecursiveMutex();
    /* Second lien (UART sur pins) : même contenu que USB, quel que soit le port connecté */
    s_uart0 = &Serial1;
    s_uart0_ok = true;
    s_uart0->begin(LOG_DUAL_BAUD, SERIAL_8N1, PIN_LOG_UART_RX, PIN_LOG_UART_TX);

    /* Forcer le niveau de log à DEBUG pour tous les tags (sinon ESP_LOGI peut être filtré) */
    esp_log_level_set("*", ESP_LOG_DEBUG);

    /* Rediriger les logs ESP-IDF (ESP_LOGx) vers notre sortie duale (Serial + UART0) */
    esp_log_set_vprintf(log_dual_vprintf);
}

/**
 * @brief Écriture de données brutes (binaire).
 *
 * @details
 * Envoie une suite d'octets sans rien changer.
 * Utile pour envoyer des fichiers ou des données codées.
 *
 * @param buf Les données à envoyer.
 * @param len La quantité de données.
 */
void log_dual_write(const uint8_t *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    bool locked = dual_lock_take();
    dual_write_nolock(buf, len);
    dual_lock_give(locked);
}

/**
 * @brief Affichage de texte simple.
 *
 * @details
 * Affiche un message à l'écran, sans retour à la ligne à la fin.
 * Exemple : `log_dual_print("Bonjour ");`
 *
 * @param str Le texte à afficher.
 */
void log_dual_print(const char *str)
{
    if (!str)
        return;
    log_dual_write((const uint8_t *)str, strlen(str));
}

/**
 * @brief Affichage de texte avec retour à la ligne.
 *
 * @details
 * Affiche un message et passe à la ligne suivante.
 * C'est la fonction la plus utilisée pour les logs.
 * Exemple : `log_dual_println("Système prêt.");`
 *
 * @param str Le texte à afficher.
 */
void log_dual_println(const char *str)
{
    if (!str)
        return;
    bool locked = dual_lock_take();
    dual_write_nolock((const uint8_t *)str, strlen(str));
    dual_write_nolock((const uint8_t *)"\r\n", 2);
    dual_lock_give(locked);
    // Serial.flush(); // Blocking, can cause WDT if USB/UART stalled
    // if (s_uart0_ok && s_uart0) s_uart0->flush();
}

/**
 * @brief Affichage de texte formaté (comme printf).
 *
 * @details
 * Permet d'insérer des variables dans le texte facilement.
 * Exemple : `log_dual_printf("Température : %d °C", 25);`
 *
 * @param fmt Le modèle de texte (avec des %d, %s, etc.).
 * @param ... Les variables à insérer.
 */
void log_dual_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0)
    {
        size_t len = (size_t)n;
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        log_dual_write((const uint8_t *)buf, len);
    }
}
