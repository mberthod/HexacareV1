/**
 * @file log_dual.cpp
 * @brief Sortie logs/données sur USB (Serial) et UART0 en parallèle.
 */

#include "config/config.h"
#include "config/pins_lexacare.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

static HardwareSerial *s_uart0 = nullptr;
static bool s_uart0_ok = false;

void log_dual_init(void) {
    Serial.begin(LOG_DUAL_BAUD);
    /* Laisser le USB CDC (ESP32-S3) se stabiliser pour éviter perte de données RX */
    delay(500);
    /* Second lien (UART sur pins) : même contenu que USB, quel que soit le port connecté */
    s_uart0 = &Serial1;
    s_uart0_ok = true;
    s_uart0->begin(LOG_DUAL_BAUD, SERIAL_8N1, PIN_LOG_UART_RX, PIN_LOG_UART_TX);
}

void log_dual_write(const uint8_t *buf, size_t len) {
    if (!buf) return;
    Serial.write(buf, len);
    if (s_uart0_ok && s_uart0) s_uart0->write(buf, len);
}

void log_dual_print(const char *str) {
    if (!str) return;
    Serial.print(str);
    if (s_uart0_ok && s_uart0) s_uart0->print(str);
}

void log_dual_println(const char *str) {
    if (!str) return;
    Serial.println(str);
    if (s_uart0_ok && s_uart0) s_uart0->println(str);
    Serial.flush();
    if (s_uart0_ok && s_uart0) s_uart0->flush();
}

void log_dual_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        size_t len = (size_t)n;
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        log_dual_write((const uint8_t *)buf, len);
    }
}
