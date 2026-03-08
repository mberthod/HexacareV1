/**
 * @file system_state.cpp
 * @brief Implémentation du stockage de l'état global du système.
 * 
 * Ce module fournit une interface thread-safe (protégée par Mutex FreeRTOS)
 * pour partager les données entre les tâches des deux cœurs (Core 0 et Core 1).
 * Il centralise les signes vitaux, les tensions, et les alertes de chute.
 */

#include "system_state.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static system_state_t s_state;              ///< Structure d'état globale (privée)
static SemaphoreHandle_t s_mutex = NULL;    ///< Mutex de protection de l'état

/**
 * @brief Initialise la structure d'état et crée le mutex.
 */
void system_state_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.sys_temp_c = -273.15f; // Valeur d'initialisation (zéro absolu)
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * @brief Verrouille l'accès à l'état système.
 */
void system_state_lock(void) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

/**
 * @brief Déverrouille l'accès à l'état système.
 */
void system_state_unlock(void) {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/**
 * @brief Met à jour l'état de détection de chute.
 * @param detected true si une chute est détectée.
 */
void system_state_set_fall_detected(bool detected) {
    system_state_lock();
    s_state.fall_detected = detected;
    system_state_unlock();
}

/**
 * @brief Récupère l'état actuel de détection de chute.
 * @return true si une chute est en cours.
 */
bool system_state_get_fall_detected(void) {
    bool v;
    system_state_lock();
    v = s_state.fall_detected;
    system_state_unlock();
    return v;
}

/**
 * @brief Met à jour les signes vitaux (Radar).
 * @param v Pointeur vers les nouvelles données.
 */
void system_state_set_vitals(const vital_signs_t *v) {
    if (!v) return;
    system_state_lock();
    memcpy(&s_state.vitals, v, sizeof(vital_signs_t));
    system_state_unlock();
}

/**
 * @brief Récupère les derniers signes vitaux.
 * @param out Destination de la copie.
 */
void system_state_get_vitals(vital_signs_t *out) {
    if (!out) return;
    system_state_lock();
    memcpy(out, &s_state.vitals, sizeof(vital_signs_t));
    system_state_unlock();
}

/**
 * @brief Met à jour l'état des rails d'alimentation (ADC).
 * @param r Pointeur vers les nouvelles mesures.
 */
void system_state_set_rails(const rails_status_t *r) {
    if (!r) return;
    system_state_lock();
    memcpy(&s_state.rails, r, sizeof(rails_status_t));
    system_state_unlock();
}

/**
 * @brief Récupère l'état des rails d'alimentation.
 * @param out Destination de la copie.
 */
void system_state_get_rails(rails_status_t *out) {
    if (!out) return;
    system_state_lock();
    memcpy(out, &s_state.rails, sizeof(rails_status_t));
    system_state_unlock();
}

/**
 * @brief Met à jour le résumé de la matrice Lidar.
 * @param m Pointeur vers le nouveau résumé.
 */
void system_state_set_matrix_summary(const matrix_summary_t *m) {
    if (!m) return;
    system_state_lock();
    memcpy(&s_state.matrix_summary, m, sizeof(matrix_summary_t));
    system_state_unlock();
}

/**
 * @brief Récupère le résumé de la matrice Lidar.
 * @param out Destination de la copie.
 */
void system_state_get_matrix_summary(matrix_summary_t *out) {
    if (!out) return;
    system_state_lock();
    memcpy(out, &s_state.matrix_summary, sizeof(matrix_summary_t));
    system_state_unlock();
}

/**
 * @brief Met à jour la température système.
 * @param temp_c Température en Celsius.
 */
void system_state_set_sys_temp(float temp_c) {
    system_state_lock();
    s_state.sys_temp_c = temp_c;
    system_state_unlock();
}

/**
 * @brief Récupère la température système.
 * @return Température en Celsius.
 */
float system_state_get_sys_temp(void) {
    float v;
    system_state_lock();
    v = s_state.sys_temp_c;
    system_state_unlock();
    return v;
}

/**
 * @brief Met à jour le niveau sonore RMS.
 * @param level Niveau sonore.
 */
void system_state_set_audio_level(int32_t level) {
    system_state_lock();
    s_state.audio_level = level;
    system_state_unlock();
}

/**
 * @brief Récupère le niveau sonore RMS.
 * @return Niveau sonore.
 */
int32_t system_state_get_audio_level(void) {
    int32_t v;
    system_state_lock();
    v = s_state.audio_level;
    system_state_unlock();
    return v;
}

/**
 * @brief Définit si le nœud est ROOT du Mesh.
 * @param is_root true si ROOT.
 */
void system_state_set_mesh_root(bool is_root) {
    system_state_lock();
    s_state.is_mesh_root = is_root;
    system_state_unlock();
}

/**
 * @brief Récupère le statut ROOT du Mesh.
 * @return true si ROOT.
 */
bool system_state_get_mesh_root(void) {
    bool v;
    system_state_lock();
    v = s_state.is_mesh_root;
    system_state_unlock();
    return v;
}

/**
 * @brief Enregistre l'adresse MAC du dispositif.
 * @param mac Chaîne de caractères MAC.
 */
void system_state_set_mac_address(const char *mac) {
    if (!mac) return;
    system_state_lock();
    strncpy(s_state.mac_address, mac, sizeof(s_state.mac_address) - 1);
    s_state.mac_address[sizeof(s_state.mac_address) - 1] = '\0';
    system_state_unlock();
}

/**
 * @brief Récupère l'adresse MAC du dispositif.
 * @param buf Buffer de destination.
 * @param buf_size Taille du buffer.
 */
void system_state_get_mac_address(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    system_state_lock();
    strncpy(buf, s_state.mac_address, buf_size - 1);
    buf[buf_size - 1] = '\0';
    system_state_unlock();
}

/**
 * @brief Retourne un pointeur direct vers la structure d'état (non protégé).
 * @warning Utiliser avec précaution, préférer les accesseurs thread-safe.
 * @return Pointeur vers s_state.
 */
system_state_t *system_state_ptr(void) {
    return &s_state;
}
