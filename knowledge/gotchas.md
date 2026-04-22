# Gotchas — LexaCare V1

Pièges connus, observés en prototypage. Un agent qui ne lit pas ce fichier
perdra plusieurs heures à chaque fois qu'il touche au projet. **Le lire en
entier** avant de toucher au code la première fois.

## Hardware

### TXB0108PW est lent

Le level-shifter TXB0108PW (3.3V ↔ 1.8V sur bus SPI ToF) a un slew rate
limité : les edges au-dessus de 1 MHz sont déformés. Symptômes :

- VL53L8CX retourne uniquement 0x00 ou 0xFF sur tous les registres
- CRC mismatch fréquent dans les logs ULD
- Init du firmware VL53L8CX timeout

**Fix** : `LEXA_TOF_SPI_FREQ_HZ = 1000000` (1 MHz) max. Pour V2, remplacer
par un TXS0108E ou passer à un LDO 1.8V dédié et garder tout en 1.8V SPI.

### GPIO 1 et 2 sont des strapping pins

Sur ESP32-S3 :

- **GPIO 1** = U0TXD pendant le boot ROM. S'il est tiré bas par une charge
  externe au reset, le bootloader n'émet pas son message série
  ("ESP-ROM:esp32s3-20210327" etc.). On croit à un boot loop alors que le
  bootloader fonctionne — juste sans log.
- **GPIO 2** = strapping indéterminé, comportement variable selon silicon revision.

Sur LexaCare V1, GPIO 1 et 2 sont assignés aux NCS des ToF #0 et #1. Si le
PCB laisse le NCS tiré bas via une résistance de pull-down, boot muet assuré.

**Fix** : dans la toute première ligne de `app_main`, configurer GPIO 1 et 2
en output HIGH **avant** toute autre init (voir `skills/vl53l8cx-init/SKILL.md` §1).
Et côté PCB, pull-up externe 10 kΩ vers 3.3V sur ces deux lignes.

### PSRAM octal vs quad

ESP32-S3-WROOM-1 **N16R8** a de la PSRAM **octal** (8 bits data). La config par
défaut ESP-IDF suppose **quad** (4 bits). Si on oublie de mettre :

```
CONFIG_SPIRAM_MODE_OCT=y
```

dans `sdkconfig.defaults`, la PSRAM boote en quad mode (compatible mais
lent), et les accès 32 bits non alignés par TFLM ralentissent d'un facteur 5.

Symptômes : inférences qui mettent 8× plus de temps que prévu, ring buffer
audio qui crépite.

**Vérification** : au boot, ESP-IDF log `SPIRAM mode: Octal`. Si `Quad` apparaît,
fix immédiat.

### Micros I2S — canal L vs R

Les deux micros sont multiplexés L/R sur le même DIN. Selon le câblage des
pins `SEL` des micros (GND ou VCC), chacun apparaît soit dans le half high
soit low du sample stéréo I2S.

Convention LexaCare V1 : `SEL=GND` → canal **L** (pair), `SEL=VCC` → canal
**R** (impair). Vérifié sur PCB rev A.

**Piège** : le pattern d'entrelacement sur ESP-IDF est LRLRLR... en 32-bit
samples, mais chaque sample est **sign-extended sur 32 bits** (vrai donnée
sur 24 MSB). Pour récupérer 16 bits propres :

```c
int16_t sample_16 = (int16_t)(sample_32 >> 16);
```

Pas `>> 14` ni `>> 15` ni autre — `>> 16`. Source d'erreur classique.

## Firmware

### `xTaskCreatePinnedToCore` : stack en bytes

Sur ESP-IDF, le paramètre `usStackDepth` est en **bytes**. Sur FreeRTOS
vanilla, c'est en **words (4 bytes)**. Si tu passes 4096 en pensant aux words,
tu obtiens 4096 bytes = stack trop petit pour une vraie tâche.

**Règle** : toujours écrire `4 * 1024` ou `4096` en commentaire "// bytes".

### TFLM : `MicroInterpreter` n'est pas thread-safe

Un `MicroInterpreter` ne doit être touché **que par une tâche à la fois**. Si
task_audio et un hypothétique task_telemetry partagent le même interpreter
pour inférer, c'est la corruption.

LexaCare contourne en ayant **deux interpreters distincts** (un par domaine),
chacun dans sa tâche. Ne pas casser cet invariant.

### `ESP_LOGI` depuis une ISR → crash

`ESP_LOGI` et consorts allouent de la mémoire et prennent un mutex. Depuis
une ISR, c'est un crash garanti. Utiliser `ESP_DRAM_LOGI` ou mieux, remonter
l'événement via `xQueueSendFromISR` et logger depuis la tâche consommatrice.

### `vTaskDelay(0)` est équivalent à `taskYIELD()`, pas à "ne rien faire"

Si tu veux vraiment ne pas dormir : `if (delay_ms > 0) vTaskDelay(pdMS_TO_TICKS(delay_ms));`

### Le watchdog IDF est activé par défaut

`CONFIG_ESP_TASK_WDT_EN=y` par défaut. Toute tâche non enregistrée qui bloque
> 5 s déclenche `Task watchdog got triggered`. Soit s'enregistrer (`esp_task_wdt_add(NULL)`)
et faire `esp_task_wdt_reset()` régulièrement, soit désactiver le WDT pour
cette tâche.

### Le bootloader second-stage peut mal gérer PSRAM au first boot

Après flash propre, premier boot échoue parfois sur "SPIRAM init failed" →
s'auto-réinitialise et redémarre, fonctionne au 2ᵉ boot. Pas un bug app,
c'est connu sur ESP-IDF 5.1+. Ignorer en dev, logger en prod. a verifier pour le 6.0

## TinyML

### Dérive MFCC Python ↔ C

**Le** piège n°1. Symptôme : accuracy 98 % en Python, 30 % sur ESP32.
Procédure de détection : `skills/mfcc-validation/SKILL.md`.

### Quantization drop > 3 %

Si l'accuracy Int8 chute de plus de 3 points par rapport au float, le dataset
représentatif est le coupable dans 80 % des cas. Pas "le modèle est trop
petit" — un ConvNet à 5000 params Int8 peut très bien reconnaître 3 classes
KWS, s'il est bien calibré.

Fix : dataset représentatif à 300–500 échantillons, équilibré par classe,
tiré uniquement du training set (jamais test/val).

### Ops manquants dans le resolver

`op not registered: DEPTHWISE_CONV_2D` → il faut ajouter `resolver.AddDepthwiseConv2D()`
dans `tflm_dual_runtime.cpp`. Voir `skills/tflm-arena/SKILL.md` §Resolver.

### Le modèle change de format entre TF versions

Un `.tflite` généré avec TF 2.15 ne charge pas forcément avec TFLM compilé
contre TF 2.13. Si `invalid argument: quantization parameters` au premier
`AllocateTensors()`, vérifier les versions croisées.

**Solution** : fixer `tensorflow` dans `tool/pyproject.toml` à la version qui
matche `esp-tflite-micro` dans `firmware/platformio.ini`. Noter la paire dans
`metadata.json` à chaque export.

## Python tool

### `pip install -e .` dans un venv + Ubuntu 25 → PEP 668

Ubuntu 25 refuse par défaut `pip install` hors venv. Toujours :

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e .
```

Ou en dernier recours : `pip install --break-system-packages` (déconseillé).

### Serial permissions

`/dev/ttyACM*` nécessite `dialout` group. À mettre une fois pour toutes :

```bash
sudo usermod -a -G dialout $USER
```

Puis se re-logger (la session actuelle ne voit pas le nouveau groupe).

### USB-CDC saturation en capture_mode

Si capture de data brute → `CRC mismatch` fréquent côté PC : le débit USB-CDC
est limité (~500 KB/s en pratique). Pour capture ToF 4×(8×8)×4 octets × 15 Hz
= 3.75 KB/s, OK. Pour capture audio stéréo 16 kHz × 4 = 64 KB/s, OK. Mais
les deux en même temps + logs ESP_LOGI → saturation.

**Fix** : en capture_mode, désactiver les `ESP_LOGI` verbeux
(`esp_log_level_set("*", ESP_LOG_WARN)` dans `app_main`).

## Divers

### `pio run` lent la première fois

PlatformIO fetche les composants managed (ESP-IDF registry) au premier build :
esp-tflite-micro, esp-dsp, etc. Peut prendre 5–10 min avec une connexion lente.
Normal. Builds suivants : 20–40 secondes.

### Mode release vs debug

Par défaut PlatformIO build en debug. Pour mesurer les perfs réelles,
`pio run -e inference_mode -v` et vérifier `-O2` dans la commande de compil.
Sinon ajouter dans `platformio.ini` :

```
build_flags = -O2 -DNDEBUG
```
