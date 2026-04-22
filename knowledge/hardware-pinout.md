# Hardware pinout — LexaCare V1

ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB PSRAM octal).

## Pinout canonique

### ToF — VL53L8CX × 4 (SPI partagé, NCS dédié, LPn via PCA9555)

| Signal | GPIO ESP32-S3 | Notes |
|--------|---------------|-------|
| SPI CLK | 4 | partagé 4 ToF, via TXB0108PW (3.3V ↔ 1.8V) |
| SPI MOSI | 15 | via TXB0108PW |
| SPI MISO | 21 | via TXB0108PW |
| NCS ToF #0 | 1 | ⚠ strapping pin — configurer output HIGH avant boot |
| NCS ToF #1 | 2 | ⚠ strapping pin — idem |
| NCS ToF #2 | 42 | OK |
| NCS ToF #3 | 41 | OK |
| LPn ToF #0 | PCA9555 P0 | I2C @ 0x20 |
| LPn ToF #1 | PCA9555 P1 | idem |
| LPn ToF #2 | PCA9555 P2 | idem |
| LPn ToF #3 | PCA9555 P3 | idem |

**Fréquence SPI max : 1 MHz** (limitation TXB0108PW). Ne pas monter sans
remplacer le level shifter.

### PCA9555 I/O expander (I2C)

| Signal | GPIO ESP32-S3 | Notes |
|--------|---------------|-------|
| I2C SDA | 11 | pull-up 4.7 kΩ externe vers 3.3V |
| I2C SCL | 12 | idem |
| Adresse | 0x20 | A0=A1=A2=GND |

P0–P3 utilisés pour LPn ToF. P4–P15 disponibles (extension future).

### Audio — 2× micros I2S stéréo

| Signal | GPIO ESP32-S3 | Notes |
|--------|---------------|-------|
| I2S BCLK | 5 | horloge bit |
| I2S WS (LRCK) | 6 | word select |
| I2S DIN | 7 | data des 2 micros mux L/R |
| Micro L select | — | tiré à GND via strap hardware |
| Micro R select | — | tiré à VCC via strap hardware |

Configuration : **I2S standard mode, 16 kHz, 16 bits par canal, format stéréo**.
Les deux canaux sont désentrelacés en software dans `i2s_stereo_mic/`.

### Alimentation

| Rail | Source | Notes |
|------|--------|-------|
| 3.3V | régulateur on-board USB | ESP32-S3 + PCA9555 + TXB0108PW côté HV |
| 1.8V | régulateur LP | VL53L8CX + TXB0108PW côté LV |
| VIN | USB-C 5V | via protection ESD |

Consommation pic mesurée : ~280 mA au boot (WiFi init + inférence + 4 ToF).
Moyenne inférence continue : ~140 mA.

## Strapping pins (rappel critique)

| GPIO | Rôle au boot | Effet si tiré à un mauvais niveau |
|------|--------------|------------------------------------|
| 0 | Boot mode select | Tiré bas = mode download |
| 3 | JTAG source | Normalement flottant |
| 45 | VDD_SPI voltage select | **Ne pas utiliser**, réservé flash |
| 46 | ROM messages on UART0 | Normalement flottant |

Et les NCS ToF :
- **GPIO 1 = U0TXD pendant le boot**. Si tiré bas par le ToF au reset, le
  bootloader n'émet pas le message série → apparence de boot loop muet.
- **GPIO 2 = strapping pin indéterminé**. Doit être en état défini au reset.

**Conséquence** : toujours init les NCS en output HIGH au tout début de
`app_main`, avant toute autre init. Voir `skills/vl53l8cx-init/SKILL.md` §1.

## Références GPIO à éviter

| GPIO | Raison |
|------|--------|
| 19, 20 | USB D+/D- (utilisés par USB-CDC en capture_mode) |
| 26, 27, 28, 29, 30, 31, 32 | Flash SPI interne (WROOM-1) |
| 33, 34, 35, 36, 37 | PSRAM octal (WROOM-1 N16R8) |

Sur un WROOM-1 N16R8, les GPIO **disponibles réellement** sont : 0, 1, 2, 3,
4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42,
45, 46, 47, 48. Les 33–37 sont **internes** au module et non routables sur le PCB.

## Mapping vers `lexa_config.h`

```c
// firmware/main/lexa_config.h

// ToF SPI
#define LEXA_TOF_SPI_CLK_GPIO       4
#define LEXA_TOF_SPI_MOSI_GPIO      15
#define LEXA_TOF_SPI_MISO_GPIO      21
#define LEXA_TOF_SPI_FREQ_HZ        1000000   // 1 MHz — limite TXB0108PW

// NCS ToF
#define LEXA_TOF_NCS_0_GPIO         1    // strapping
#define LEXA_TOF_NCS_1_GPIO         2    // strapping
#define LEXA_TOF_NCS_2_GPIO         42
#define LEXA_TOF_NCS_3_GPIO         41

// I2C PCA9555
#define LEXA_I2C_SDA_GPIO           11
#define LEXA_I2C_SCL_GPIO           12
#define LEXA_I2C_FREQ_HZ            400000
#define LEXA_PCA9555_ADDR           0x20

// I2S audio
#define LEXA_I2S_BCLK_GPIO          5
#define LEXA_I2S_WS_GPIO            6
#define LEXA_I2S_DIN_GPIO           7
#define LEXA_I2S_SAMPLE_RATE_HZ     16000
```

En cas de conflit entre ce fichier et `lexa_config.h` compilé : `lexa_config.h`
fait foi (c'est ce qui tourne), mais signaler la divergence à Mathieu pour
mise à jour de ce document.
