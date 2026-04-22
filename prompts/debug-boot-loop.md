# PROMPT : debug boot loop ou Guru Meditation

Méthode **déterministe** pour diagnostiquer un firmware LexaCare qui ne boote
pas correctement. Suivre dans l'ordre — ne pas sauter les étapes.

## Symptômes couverts

- Boot silencieux (rien sur série) et reset en boucle
- `Guru Meditation Error: Core X panic'ed (reason)`
- `rst:0xX (reason)` suivi d'un autre reset
- `assert failed: ...`
- Firmware démarre, logue quelques messages, puis freeze

## Pré-conditions

- Série accessible : `pio device monitor` ou `screen /dev/ttyACM0 115200`
- Baudrate = 115200 (fixé dans `platformio.ini`)
- Backtrace activé : `CONFIG_COMPILER_OPTIMIZATION_LEVEL` = "Debug" (-Og) en phase diagnostic

## Étape 1 : capturer le log complet

Flasher et tout de suite monitorer :

```bash
cd firmware
pio run -e inference_mode -t upload && pio device monitor
```

Laisser tourner 30 secondes, copier tout le log dans un fichier. La première
ligne utile sera du type :

```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x1 (POWERON),boot:0x0 (DOWNLOAD_BOOT_ONLY)
```

## Étape 2 : identifier la famille de problème

### Cas 2A : aucune ligne du bootloader ROM

Pas de `ESP-ROM:` au démarrage → deux causes possibles :

1. **USB/série pas configurée** — vérifier `/dev/ttyACM*` visible, droits dialout, câble USB data (pas charge seule).
2. **GPIO 1 tiré bas au reset** — GPIO 1 = UART0_TX pendant le boot ROM. Sur LexaCare V1 c'est NCS ToF #0, si pull-down externe ou charge excessive, le bootloader n'émet rien.

Fix pour cas 2 : vérifier pull-up externe sur GPIO 1 (10 kΩ vers 3.3V), et
que `app_main` force GPIO 1 en output HIGH **avant** toute autre init (voir
`skills/vl53l8cx-init/SKILL.md` §1).

### Cas 2B : `rst:0x8 (TG1WDT_SYS_RESET)` en loop

Watchdog timer groupe 1. Soit le bootloader boucle avant `app_main` (rare,
= PSRAM corrompue), soit `app_main` bloque > 30 s sur init.

Fix : brancher `idf.py monitor` avec `--print_filter "*:V"` pour voir TOUS
les logs, trouver où ça coince. Typiquement l'init VL53L8CX ou I2C PCA9555
qui timeout.

### Cas 2C : `Guru Meditation Error: StoreProhibited` / `LoadProhibited`

Accès mémoire invalide. 99 % du temps = pointeur null ou buffer non initialisé.

Actions :

1. Noter l'adresse faultive : `EXCVADDR: 0x00000000` (null) ou `0x3c...` (flash, lecture seule écrite) ou autre.
2. Décoder la backtrace avec `esp-idf-monitor` (fait auto si on utilise `pio device monitor`).
3. Chercher la ligne source dans la backtrace, pattern typique : une struct allouée mais pas initialisée, ou un handle FreeRTOS (`QueueHandle_t`) utilisé avant `xQueueCreate`.

### Cas 2D : `Guru Meditation Error: IllegalInstruction`

Le PC saute dans une zone non-code. Causes :

1. **Stack overflow** — la tâche a débordé sa stack et le return address pointe n'importe où. Activer `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`, rebuild, reflash. Le canary détecte le débord et logue clairement la tâche coupable.
2. **Pointeur de fonction corrompu** — rare, debug via backtrace.

### Cas 2E : `assert failed: xQueueGenericSend`

Une queue non initialisée reçoit un send. Chercher tous les `xQueueCreate`
dans le code, vérifier qu'ils sont appelés AVANT le premier `xQueueSend` sur
cette queue.

Pattern sûr : créer la queue comme `static QueueHandle_t q = NULL;` et la
créer dans une init explicite appelée au début de `app_main`.

### Cas 2F : `E (xxx) spi_master: spi_bus_initialize(XX): already initialized`

Deux appels à `spi_bus_initialize` pour le même host. Vérifier que chaque
composant utilisant SPI fait l'init dans une fonction `_init` appelée une
seule fois depuis `app_main`, pas automatiquement à son chargement.

## Étape 3 : vérifications structurelles

Si la famille n'est pas évidente, auditer ces points dans l'ordre :

### 3.1 — `sdkconfig.defaults`

```bash
grep -E "SPIRAM|PSRAM" firmware/sdkconfig.defaults
```

Doit contenir **exactement** :

```
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
```

Si `SPIRAM_MODE_QUAD=y` à la place de `SPIRAM_MODE_OCT=y`, PSRAM inoperante
sur N16R8. Fix et rebuild.

### 3.2 — `partitions.csv`

Doit couvrir au moins `nvs`, `factory`, et `storage`. Si l'app > 2 MB,
`factory` doit faire au moins 3 MB. Sinon flash partielle → crash au premier
malloc tardif.

### 3.3 — PSRAM réellement détectée

Au boot, chercher dans les logs :

```
I (xxx) esp_psram: Found 8MB PSRAM device
I (xxx) esp_psram: Speed: 80MHz
I (xxx) cpu_start: Pro cpu up.
```

Si `Found 2MB` ou `Found 4MB` au lieu de 8MB : PSRAM de mauvaise taille ou
boot en quad mode. Retour étape 3.1.

### 3.4 — Strapping pins

Au boot, la ligne :

```
rst:0x1 (POWERON),boot:0x0 (SPI_FLASH_BOOT)
```

Doit montrer `boot:0x0` (boot flash normal). Si `boot:0x2` (DOWNLOAD_BOOT) :
GPIO 0 tiré bas au reset, câble USB à débrancher-rebrancher ou bouton BOOT
enfoncé par erreur.

## Étape 4 : isolation par désactivation

Si les étapes 1–3 n'ont rien donné, commenter progressivement des parties de
`app_main` pour isoler le coupable :

```c
void app_main(void) {
    ESP_LOGI(TAG, "app_main start");

    // 1. Init critical GPIO
    init_strapping_pins();
    ESP_LOGI(TAG, "strapping ok");

    // 2. Init PCA9555
    pca9555_init(...);
    ESP_LOGI(TAG, "pca ok");

    // 3. Init SPI
    init_spi_bus();
    ESP_LOGI(TAG, "spi ok");

    // 4. Init ToF (le plus risqué)
    // init_tof_array();
    // ESP_LOGI(TAG, "tof ok");

    // 5. Créer tâches
    // xTaskCreatePinnedToCore(task_audio_entry, ...);
    // xTaskCreatePinnedToCore(task_vision_entry, ...);

    ESP_LOGI(TAG, "app_main done");
}
```

Décommenter par étape, noter à quelle étape le boot casse. Une fois localisé,
focus debug sur cette fonction.

## Étape 5 : backtrace decoder

Si on a une backtrace type `Backtrace: 0x40375f37:0x3fca7f70 0x...` :

```bash
# Avec PlatformIO
pio run -t menuconfig     # pour activer debug symbols si pas déjà fait
pio debug                 # ou bien
# addr2line sur le .elf
xtensa-esp32s3-elf-addr2line -e .pio/build/inference_mode/firmware.elf \
    0x40375f37
```

Ça donne fichier:ligne — en général on identifie le bug en 30 sec.

## Anti-patterns à ne pas faire

- **Ne pas** ajouter de `vTaskDelay(pdMS_TO_TICKS(100))` au hasard "pour que ça marche". Soit un ordre d'init précis est requis (dans quel cas il doit être documenté), soit un synchro via event group est la bonne solution.
- **Ne pas** wrapper l'init dans un `try/catch` équivalent en C (aka tester tous les `esp_err_t` sans agir dessus). Une init qui échoue doit faire crasher proprement via `ESP_ERROR_CHECK` pour qu'on voie le vrai problème.
- **Ne pas** commenter définitivement l'init qui pose problème sans ticket de suivi. Un firmware qui "marche" parce qu'on a désactivé l'init ToF n'est pas un firmware qui marche.

## Si rien ne marche

Reflasher le bootloader + partition table + app depuis zéro :

```bash
pio run -e inference_mode -t erase
pio run -e inference_mode -t upload
```

`erase` efface la flash entière. Certains états NVS corrompus ne sont résolus
que par un erase complet.
