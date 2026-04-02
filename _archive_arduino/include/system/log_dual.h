/**
 * @file log_dual.h
 * @brief Logs et données sur USB (Serial) et UART0 en parallèle — quel que soit le port connecté.
 */

#ifndef LOG_DUAL_H
#define LOG_DUAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Baud rate commun pour USB et UART0. */
#define LOG_DUAL_BAUD 921600

/**
 * @brief Initialise Serial (USB) et UART0 (pins LOG). À appeler en premier dans setup().
 */
void log_dual_init(void);

/**
 * @brief Envoie des octets sur les deux liens (USB + UART0).
 */
void log_dual_write(const uint8_t *buf, size_t len);

/**
 * @brief Envoie une chaîne C sur les deux liens (sans retour à la ligne).
 */
void log_dual_print(const char *str);

/**
 * @brief Envoie une chaîne C + \n sur les deux liens.
 */
void log_dual_println(const char *str);

/**
 * @brief printf vers les deux liens (buffer interne 256 octets).
 */
void log_dual_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_DUAL_H */
