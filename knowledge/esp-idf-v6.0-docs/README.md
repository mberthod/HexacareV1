# ESP-IDF v6.0 — Documentation officielle (format Markdown)

**À lire en priorité par l'IA** qui utilise ce corpus (Claude Code, Khoj, Cursor, Continue…).

Ce dossier contient **351 fichiers Markdown** générés depuis les sources Sphinx officielles
d'ESP-IDF v6.0 (snapshot du 19 mars 2026). L'arborescence d'origine est préservée pour
que chaque fichier reste atomique et recherchable indépendamment.

---

## Règles que l'IA doit suivre

1. **Cette doc fait foi pour ESP-IDF v6.0.** Si une réponse basée sur la mémoire contredit
   un fichier de ce dossier, le fichier gagne. Toujours citer le chemin du fichier consulté
   (ex. `api-reference/peripherals/spi_master.md`).

2. **Placeholders Sphinx non résolus.** Les textes entre accolades comme :
   - `{IDF_TARGET_NAME}` → nom de la cible (ESP32, ESP32-S3, ESP32-P4, ESP32-C6, etc.)
   - `{IDF_TARGET_PATH_NAME}` → nom normalisé (esp32, esp32s3…)
   - `{IDF_TARGET_TRM_EN_URL}` → URL du Technical Reference Manual

   …ne sont **pas remplacés** dans les Markdown (Sphinx les résout au build en fonction
   de la cible compilée). Quand tu réponds, **demande ou déduis la cible** (ex. depuis
   `CMakeLists.txt`, `sdkconfig`, ou `idf.py set-target`) puis substitue mentalement.

3. **Sections conditionnelles par cible.** Les blocs :
   ```
   <!-- Only for: esp32s3 -->
   ```
   …indiquent que le contenu qui suit **ne s'applique qu'à la cible listée**. Si la cible
   du projet de l'utilisateur est différente, ignorer ce bloc.

4. **Admonitions Sphinx converties en blockquote** :
   `> **Note**`, `> **Warning**`, `> **Important**`, `> **Caution**`, `> **Tip**`, `> **SeeAlso**`.
   Les **Warning** et **Caution** sont critiques — ne jamais les omettre quand tu génères
   du code ou une explication.

5. **Références croisées.** Les `:ref:` Sphinx (ex. `` `spi_master_on_spi1_bus` ``) sont
   restés sous forme de texte. Ils pointent vers une ancre dans un autre fichier du corpus.
   Pour retrouver la cible, grep le label dans `_sources/` (ou dans ce dossier sur le titre).

---

## Où chercher en priorité selon le besoin

| Type de question | Dossier à consulter |
|---|---|
| API d'un périphérique (SPI, I2C, UART, ADC, I2S, GPIO, LEDC, MCPWM, TWAI/CAN, etc.) | `api-reference/peripherals/` |
| Réseau (Wi-Fi, Ethernet, ESP-NOW, Thread, Zigbee) | `api-reference/network/` |
| BLE / Bluetooth (NimBLE, Bluedroid, GAP, GATT) | `api-reference/bluetooth/` + `api-guides/ble/` |
| FreeRTOS (tâches, queues, sémaphores, tickless idle, SMP) | `api-reference/system/freertos*.md`, `api-guides/freertos*.md` |
| Système (mémoire, heap, IPC, interruptions, partitions, OTA) | `api-reference/system/` + `api-guides/` |
| Storage (NVS, SPIFFS, LittleFS, FAT, Wear Levelling, SD Card) | `api-reference/storage/` |
| Protocoles app (HTTP, HTTPS, MQTT, WebSocket, mDNS, SNTP, ASIO) | `api-reference/protocols/` |
| **Breaking changes v6.0 (⚠️ critique)** | **`migration-guides/release-6.x/`** |
| Breaking changes v5.x | `migration-guides/release-5.x/` |
| Build system, CMake, composants, Kconfig, linker | `api-guides/build-system*.md`, `api-guides/linker-script-generation.md` |
| Sécurité (Secure Boot v2, Flash Encryption, HMAC, Digital Signature) | `security/` |
| Dev kits, modules, variantes de chips | `hw-reference/` |
| Debug (GDB, JTAG, core dump, heap trace, app trace, gcov) | `api-guides/` (plusieurs fichiers `*debug*`, `*trace*`) |
| Installation toolchain (Linux, macOS, Windows) | `get-started/` |

---

## Anti-erreurs fréquentes sur ESP-IDF v6.0

Avant de générer du code, **toujours vérifier** dans `migration-guides/release-6.x/` si une
API a changé. Quelques catégories de régressions classiques entre versions majeures :

- Drivers legacy déplacés vers `esp_driver_*` (composants séparés) — vérifier les includes.
- Changements de signature de fonctions SPI / I2C / UART (nouveau modèle de handle).
- `esp_event` : nouveaux event IDs, anciens dépréciés.
- Wi-Fi : changements de structures de config, politique de mémoire.
- FreeRTOS v10.5+ : nouveaux symboles SMP, renommages de macros.
- Cibles ajoutées/retirées dans cette version.

Chaque fois qu'une réponse touche à une API, **vérifier le fichier correspondant dans
`api-reference/`** au lieu de se fier à la mémoire.

---

## Index complet

Voir `INDEX.md` à la racine pour la liste des 351 fichiers avec leur titre.
