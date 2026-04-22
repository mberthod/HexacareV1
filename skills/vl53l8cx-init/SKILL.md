# SKILL : vl53l8cx-init

Séquence d'init du driver VL53L8CX (ULD ST) pour le mode multi-capteur LexaCare
(4 capteurs partageant un bus SPI, sélectionnés via NCS et LPn).

## Contexte hardware LexaCare V1

- 4 capteurs VL53L8CX sur un bus SPI commun (CLK, MOSI, MISO partagés)
- **NCS** dédié par capteur : GPIO 1, 2, 42, 41 (⚠ 1 et 2 = strapping pins)
- **LPn** (low-power / reset) via PCA9555 I2C @ 0x20, P0–P3
- Level-shifter **TXB0108PW** entre ESP32 3.3V et VL53L8CX 1.8V
  → **SPI max 1 MHz** (limite du TXB0108PW)
- Séquence boot : tous les LPn à LOW (capteurs éteints), puis init un par un
  en remontant LPn_i à HIGH

## Séquence canonique

### 1. Avant tout : pins strapping en état sûr

```c
// Dans le tout début de app_main, AVANT l'init SPI
gpio_config_t strap_cfg = {
    .pin_bit_mask = (1ULL << 1) | (1ULL << 2),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
ESP_ERROR_CHECK(gpio_config(&strap_cfg));
gpio_set_level(1, 0);  // NCS ToF0 désactivé
gpio_set_level(2, 0);  // NCS ToF1 désactivé
```

Ces deux lignes **doivent** exister sinon le comportement au reset est
indéterminé (bootloader peut mal lire la stratégie de boot).

### 2. Init PCA9555 et tous LPn à LOW

```c
ESP_ERROR_CHECK(pca9555_init(I2C_NUM_0, 0x20));
ESP_ERROR_CHECK(pca9555_set_output_mode(0x0F));  // P0..P3 en output
ESP_ERROR_CHECK(pca9555_write_output(0x00));     // tous à LOW → capteurs en reset
vTaskDelay(pdMS_TO_TICKS(10));
```

### 3. Init bus SPI (1 MHz, mode 3)

```c
spi_bus_config_t buscfg = {
    .miso_io_num = 21,
    .mosi_io_num = 15,
    .sclk_io_num = 4,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4096,
};
ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
```

### 4. Init capteurs un par un

Pour chaque capteur `i` de 0 à 3 :

```c
// a) Relâcher LPn du capteur i uniquement
pca9555_write_output(1 << i);    // seul ce LPn à HIGH
vTaskDelay(pdMS_TO_TICKS(2));    // t_boot VL53L8CX = 1.2 ms min

// b) Ajouter le device SPI avec son NCS
const int ncs_gpios[4] = { 1, 2, 42, 41 };
spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 1000000,   // 1 MHz — limite TXB0108PW
    .mode = 3,
    .spics_io_num = ncs_gpios[i],
    .queue_size = 2,
    .flags = SPI_DEVICE_HALFDUPLEX,
};
ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &tof_handles[i]));

// c) Init ULD ST
VL53L8CX_Configuration cfg = { .platform = { .tof_id = i } };
if (vl53l8cx_init(&cfg) != VL53L8CX_STATUS_OK) {
    ESP_LOGE(TAG, "VL53L8CX #%d init failed", i);
    // gestion d'erreur — voir §Recovery
}

// d) Démarrer le ranging
vl53l8cx_set_resolution(&cfg, VL53L8CX_RESOLUTION_8X8);
vl53l8cx_set_ranging_frequency_hz(&cfg, 15);  // 15 Hz stable avec 4 capteurs
vl53l8cx_start_ranging(&cfg);
```

### 5. Lecture en boucle (task_vision)

```c
for (;;) {
    for (int i = 0; i < 4; i++) {
        uint8_t ready = 0;
        vl53l8cx_check_data_ready(&cfg[i], &ready);
        if (ready) {
            VL53L8CX_ResultsData res;
            vl53l8cx_get_ranging_data(&cfg[i], &res);
            // Assembler la frame 16×8 (2 capteurs de largeur, 2 de hauteur)
            // → pousser vers TFLM vision
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

## Recovery sur échec init

Si un capteur rate son init :

1. Passer son LPn à LOW (force reset)
2. `spi_bus_remove_device(tof_handles[i])`
3. Log l'échec, continuer avec les autres
4. Marquer la frame correspondante comme invalide (tous zéros) pour TFLM —
   le modèle doit être entraîné avec des échantillons "capteur OFF" pour
   gérer ce cas gracefully

## Gotchas

- **Ne jamais** initialiser plusieurs capteurs simultanément avec LPn partagé. Le firmware par défaut de VL53L8CX démarre sur l'adresse I2C 0x29 (ou le SPI equivalent) — si deux capteurs sont réveillés en même temps, collision.
- Sur LexaCare V1 c'est du SPI, donc pas de conflit d'adresse I2C, MAIS l'ULD ST contient des routines de config qui supposent un capteur à la fois.
- Si les 4 capteurs ne sortent que des zéros : **vérifier d'abord le TXB0108PW**. Si SPI à 2 MHz+, les edges sont déformés, le CRC échoue, les registres lisent 0. Redescendre à 1 MHz.
- GPIO 1 et 2 en strapping : si le firmware boote en looping sans message série, c'est que GPIO 1 (= UART0_TX_BOOT) est tiré bas par le ToF #0 à un mauvais moment. Garder le NCS à HIGH au repos (SPI mode 3, CS active low) et forcer l'output state avant init SPI.
- Le firmware interne des VL53L8CX est chargé par l'ULD à chaque init (gros blob, ~80 KB). Charger successivement = lent (~500 ms par capteur). Optimisation possible : charger le firmware 1 fois puis le broadcaster, mais hors scope V1.

## Référence ULD

Voir `knowledge/references.md` §VL53L8CX pour le lien ST officiel. Toujours
utiliser la dernière version ULD (≥ v2.0.0) — les antérieures ont des bugs
sur le mode 8×8.
