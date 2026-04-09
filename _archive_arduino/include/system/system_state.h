/**
 * @file system_state.h
 * @brief Structures d'état partagées et accesseurs pour le firmware Lexacare
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Signes vitaux (radar) ----------
typedef struct {
    bool     presence;
    uint16_t heart_rate_bpm;
    uint16_t breath_rate_bpm;
    uint16_t target_distance_mm;
    uint32_t last_update_ms;
} vital_signs_t;

// ---------- Rails tension (ADC) ----------
typedef struct {
    uint32_t v_in_mv;
    uint32_t v_batt_mv;
    uint32_t v_1v8_mv;
    uint32_t v_3v3_mv;
    uint32_t last_update_ms;
} rails_status_t;

// ---------- Résumé matrice Lidar (pour MQTT) ----------
typedef struct {
    uint16_t min_mm;
    uint16_t max_mm;
    uint32_t sum_mm;
    uint16_t valid_zones;
    uint32_t last_update_ms;
} matrix_summary_t;

// ---------- État global système ----------
typedef struct {
    bool              fall_detected;
    vital_signs_t     vitals;
    rails_status_t    rails;
    matrix_summary_t  matrix_summary;
    float             sys_temp_c;
    int32_t           audio_level;      // niveau sonore (RMS ou peak)
    bool              is_mesh_root;
    char              mac_address[18];
    uint32_t          last_mqtt_publish_ms;
} system_state_t;

// Accesseurs (thread-safe via mutex interne)
void system_state_init(void);
void system_state_lock(void);
void system_state_unlock(void);

void system_state_set_fall_detected(bool detected);
bool system_state_get_fall_detected(void);

void system_state_set_vitals(const vital_signs_t *v);
void system_state_get_vitals(vital_signs_t *out);

void system_state_set_rails(const rails_status_t *r);
void system_state_get_rails(rails_status_t *out);

void system_state_set_matrix_summary(const matrix_summary_t *m);
void system_state_get_matrix_summary(matrix_summary_t *out);

void system_state_set_sys_temp(float temp_c);
float system_state_get_sys_temp(void);

void system_state_set_audio_level(int32_t level);
int32_t system_state_get_audio_level(void);

void system_state_set_mesh_root(bool is_root);
bool system_state_get_mesh_root(void);

void system_state_set_mac_address(const char *mac);
void system_state_get_mac_address(char *buf, size_t buf_size);

// Pointeur vers l'état (pour accès direct sous lock)
system_state_t *system_state_ptr(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_STATE_H
