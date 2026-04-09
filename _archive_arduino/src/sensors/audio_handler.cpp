/**
 * @file audio_handler.cpp
 * @brief Gestion de l'acquisition audio I2S (DMA) pour les micros ICS-43434.
 * 
 * Ce module configure l'interface I2S de l'ESP32-S3 pour lire les échantillons
 * audio des microphones numériques. Il calcule le niveau sonore RMS et remplit
 * un buffer pour de futurs traitements (FFT).
 */

#include "config/config.h"
#include "audio_handler.h"
#include "config/pins_lexacare.h"
#include "system/system_state.h"
#include <driver/i2s.h>
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char* TAG = "AUDIO";

#define I2S_PORT      I2S_NUM_0             ///< Port I2S utilisé
#define I2S_BCK_PIN   PIN_MIC_SCK           ///< Broche Bit Clock
#define I2S_WS_PIN    PIN_MIC_WS            ///< Broche Word Select (LRCK)
#define I2S_DATA_PIN  PIN_MIC_SD            ///< Broche Data In

static int32_t s_audio_level = 0;           ///< Niveau sonore actuel (RMS)
static int16_t s_fft_buffer[AUDIO_FFT_SIZE]; ///< Buffer pour analyse fréquentielle
static bool s_initialized = false;          ///< État d'initialisation du driver

/**
 * @brief Calcule la valeur RMS (Root Mean Square) d'un bloc d'échantillons.
 * @param samples Pointeur vers le tableau d'échantillons 16 bits.
 * @param n Nombre d'échantillons.
 * @return Valeur RMS calculée.
 */
static int32_t compute_rms(int16_t *samples, size_t n) {
    if (n == 0) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = samples[i];
        sum += (int64_t)v * v;
    }
    return (int32_t)sqrt((double)(sum / (int64_t)n));
}

/**
 * @brief Initialise le périphérique I2S et l'alimentation des micros.
 * @return true si le driver est installé avec succès.
 */
bool audio_handler_init(void) {
    // Activation de l'alimentation des microphones
    pinMode(PIN_POWER_MIC, OUTPUT);
    digitalWrite(PIN_POWER_MIC, HIGH);
    delay(10);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = AUDIO_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_24BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = AUDIO_DMA_BUF_COUNT;
    cfg.dma_buf_len = AUDIO_DMA_BUF_LEN;
    cfg.use_apll = false;

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num = I2S_BCK_PIN;
    pin_cfg.ws_io_num = I2S_WS_PIN;
    pin_cfg.data_in_num = I2S_DATA_PIN;
    pin_cfg.data_out_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec installation driver I2S (err=0x%x)", err);
        return false;
    }
    err = i2s_set_pin(I2S_PORT, &pin_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec configuration pins I2S (err=0x%x)", err);
        return false;
    }
    
    // Configuration de l'horloge pour le mode mono
    i2s_set_clk(I2S_PORT, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_24BIT, I2S_CHANNEL_MONO);

    memset(s_fft_buffer, 0, sizeof(s_fft_buffer));
    s_initialized = true;
    ESP_LOGI(TAG, "Driver I2S initialisé (16kHz, 24bit)");
    return true;
}

/**
 * @brief Lit un bloc de données DMA et met à jour le niveau sonore.
 * 
 * Cette fonction est appelée périodiquement par la tâche audio. Elle lit les
 * échantillons bruts, calcule le RMS et met à jour l'état système.
 */
void audio_handler_process(void) {
    if (!s_initialized) return;
    size_t bytes_read = 0;
    int16_t samples[AUDIO_DMA_BUF_LEN];
    size_t want = sizeof(samples);
    
    // Lecture bloquante des données DMA
    esp_err_t err = i2s_read(I2S_PORT, samples, want, &bytes_read, portMAX_DELAY);
    if (err != ESP_OK || bytes_read == 0) {
        ESP_LOGV(TAG, "I2S read error or no data");
        return;
    }

    size_t num_samples = bytes_read / sizeof(int16_t);
    s_audio_level = compute_rms(samples, num_samples);
    ESP_LOGV(TAG, "Niveau audio: %d", s_audio_level);
    
    // Mise à jour de l'état partagé
    system_state_set_audio_level(s_audio_level);

    // Copie dans le buffer FFT pour traitement ultérieur
    size_t copy = (num_samples < (size_t)AUDIO_FFT_SIZE) ? num_samples : (size_t)AUDIO_FFT_SIZE;
    memcpy(s_fft_buffer, samples, copy * sizeof(int16_t));
    if (copy < (size_t)AUDIO_FFT_SIZE)
        memset(s_fft_buffer + copy, 0, (AUDIO_FFT_SIZE - copy) * sizeof(int16_t));
}

/**
 * @brief Retourne le dernier niveau sonore calculé.
 * @return Niveau RMS.
 */
int32_t audio_handler_get_level(void) {
    return s_audio_level;
}

/**
 * @brief Copie le contenu du buffer FFT vers un buffer externe.
 * @param out Pointeur vers le buffer de destination.
 * @param len Taille demandée.
 */
void audio_handler_get_fft_buffer(int16_t *out, size_t len) {
    if (!out) return;
    size_t n = (len < (size_t)AUDIO_FFT_SIZE) ? len : (size_t)AUDIO_FFT_SIZE;
    memcpy(out, s_fft_buffer, n * sizeof(int16_t));
}
