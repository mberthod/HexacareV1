/**
 * @file tmp117_handler.h
 * @brief Température haute précision TMP117 (I2C)
 */

#ifndef TMP117_HANDLER_H
#define TMP117_HANDLER_H

#include "config/config.h"
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

#define TMP117_I2C_ADDR (0x48)
#define TMP117_REG_TEMP (0x00)

// Initialise et vérifie présence du TMP117 sur le bus.
bool tmp117_handler_init(void);

// Lit la température en °C et met à jour system_state (sys_temp).
float tmp117_handler_read_temp_c(void);

#ifdef __cplusplus
}
#endif

#endif // TMP117_HANDLER_H
