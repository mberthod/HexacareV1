/**
 * @file led_manager.cpp
 * @brief L'Indicateur Visuel (Gestion de la LED).
 *
 * @details
 * Ce module permet au boîtier de communiquer avec l'humain grâce à sa LED colorée.
 * C'est le seul moyen de savoir ce qui se passe à l'intérieur sans ordinateur.
 *
 * Code des couleurs :
 * - **BLEU Clignotant** : "Je cherche un chef..." (Scanning).
 * - **VERT Pulsant** : "Tout va bien, je suis connecté au réseau" (Connected).
 * - **VIOLET Fixe** : "Je suis le CHEF (ROOT), branché au PC".
 * - **ORANGE Clignotant** : "Ne pas éteindre ! Je fais une mise à jour" (OTA).
 * - **ROUGE** : "Erreur critique" (Panne).
 * - **Flash CYAN** : "J'ai reçu un message".
 * - **Flash BLANC** : "J'ai envoyé un message".
 */

#include "system/led_manager.h"
#include "config/config.h"
#include "config/pins_lexacare.h"
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static Adafruit_NeoPixel s_pixel(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
static LedState s_current_state = LED_STATE_OFF;
static uint32_t s_flash_end_time = 0;
static uint32_t s_flash_color = 0;

void led_manager_init(void)
{
    s_pixel.begin();
    s_pixel.setBrightness(50);
    s_pixel.show();
}

void led_manager_set_state(LedState state)
{
    s_current_state = state;
}

void led_flash_rx(void)
{
    s_flash_color = s_pixel.Color(0, 255, 255); // Cyan
    s_flash_end_time = xTaskGetTickCount() + pdMS_TO_TICKS(50);
}
void led_flash_yellow_rx(void)
{
    s_flash_color = s_pixel.Color(0, 255, 0); // Yellow
    s_flash_end_time = xTaskGetTickCount() + pdMS_TO_TICKS(50);
}

void led_flash_tx(void)
{
    s_flash_color = s_pixel.Color(255, 255, 255); // Blanc
    s_flash_end_time = xTaskGetTickCount() + pdMS_TO_TICKS(50);
}

void led_manager_task(void *pv)
{
    (void)pv;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 20Hz update
    uint32_t counter = 0;

    for (;;)
    {
        uint32_t now = xTaskGetTickCount();
        uint32_t color = 0;

        // Gestion des flashs prioritaires
        if (now < s_flash_end_time)
        {
            color = s_flash_color;
        }
        else
        {
            // État de base
            switch (s_current_state)
            {
            case LED_STATE_SCANNING:
                // Blink Bleu rapide (200ms)
                if ((counter % 4) < 2)
                    color = s_pixel.Color(0, 0, 255);
                break;
            case LED_STATE_CONNECTED:
                // Heartbeat Vert (Pulsation)
                {
                    // Simuler une respiration
                    float val = (exp(sin(counter * 0.1)) - 0.36787944) * 108.0;
                    color = s_pixel.Color(0, (uint8_t)val, 0);
                }
                break;
            case LED_STATE_ROOT:
                // Violet Fixe (avec légère variation pour montrer vie)
                color = s_pixel.Color(100, 0, 200);
                break;
            case LED_STATE_OTA:
                // Blink Orange Rapide (générique)
                if ((counter % 2) == 0)
                    color = s_pixel.Color(255, 165, 0);
                break;
            case LED_STATE_OTA_SERIAL:
                // OTA Série (0x01) : Violet pulsé = mise à jour ROOT par câble
                if ((counter % 6) < 3)
                    color = s_pixel.Color(180, 0, 255);
                else
                    color = s_pixel.Color(80, 0, 120);
                break;
            case LED_STATE_OTA_MESH:
                if ((counter % 6) < 3)
                    color = s_pixel.Color(0, 100, 255);
                else
                    color = s_pixel.Color(0, 50, 150);
                break;
            case LED_STATE_OTA_MESH_ROOT:
                // OTA Mesh ROOT : orange fixe (tâches arrêtées, réception chunks)
                color = s_pixel.Color(255, 165, 0);
                break;
            case LED_STATE_OTA_MESH_CHILD:
                // OTA Mesh enfant : rouge fade vers bleu (respiration)
                {
                    float t = (counter % 80) / 80.0f;
                    uint8_t r = (uint8_t)(255 * (1.0f - t));
                    uint8_t b = (uint8_t)(100 + 155 * t);
                    color = s_pixel.Color(r, 0, b);
                }
                break;
            case LED_STATE_ORPHAN:
                // Perte parent : flash Orange/Rouge (reconnexion en cours)
                if ((counter % 4) < 2)
                    color = s_pixel.Color(255, 100, 0);
                else
                    color = s_pixel.Color(255, 0, 0);
                break;
            case LED_STATE_ERROR:
                color = s_pixel.Color(255, 0, 0);
                break;
            default:
                color = 0;
                break;
            }
        }

        s_pixel.setPixelColor(0, color);
        s_pixel.show();

        counter++;
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
