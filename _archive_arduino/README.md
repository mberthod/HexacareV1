# Lexacare Firmware V1 – Mode Sandbox ESP-NOW Flooding

Firmware de test pour **ESP32-S3 (16 MB Flash)** : mesh pur **ESP-NOW par inondation** (sans routeur WiFi), protocole binaire 32 octets, passerelle série (JSON + OTA), propagation OTA décentralisée par flooding.

**Objectifs** : radio mesh ESP-NOW (broadcast FF:FF:FF:FF:FF:FF), cache anti-doublon 50 msgId, TTL et jitter 5–50 ms, trames Data / OTA_ADV / OTA_CHUNK, Gateway (Node 0) → JSON sur Serial, OTA poussé depuis le PC via Série puis diffusé en broadcast.

---

## Architecture système (FreeRTOS)

| Cœur | Tâche | Priorité | Rôle |
|------|--------|----------|------|
| **Core 0** | `espnowRxTask` | 3 | Consomme `g_queue_espnow_rx`, appelle `espnow_mesh_handle_packet()` (cache, TTL, jitter, dispatch Data/OTA). |
| **Core 0** | `espnowTxTask` | 2 | Lit `g_queue_espnow_tx`, construit Header + LexaFullFrame, envoie en broadcast. |
| **Core 0** | `serialGatewayTask` | 1 | **Si Gateway** : lit Serial (OTA start 38 octets puis blocs 200 octets), envoie OTA_ADV puis OTA_CHUNK en broadcast. |
| **Core 1** | `sensorSimulationTask` | 2 | 1 Hz exact (`vTaskDelayUntil`), génère LexaFullFrame simulée, pousse dans `g_queue_espnow_tx`. |

- **Callback ESP-NOW** : copie mac + payload dans `g_queue_espnow_rx` (pas de traitement lourd dans le callback).
- **Synchronisation** : mutex sur le cache msgId ; mutex OTA pour `Update.write` et état ; queues FreeRTOS pour RX/TX.

---

## Protocole binaire

### En-tête commun (8 octets) – `EspNowMeshHeader`

- `msgId` (uint32_t) : ID unique (anti-doublon).
- `msgType` (uint8_t) : 0x01 Data, 0x02 OTA_ADV, 0x03 OTA_CHUNK.
- `ttl` (uint8_t) : décrémenté à chaque saut.
- `sourceNodeId` (uint16_t) : auteur (2 derniers octets MAC).

### Types de paquets

| Type | Payload | Taille max paquet (header + payload) |
|------|---------|--------------------------------------|
| **Data** | LexaFullFrame (32 octets) | 8 + 32 = **40** ≤ 250 |
| **OTA_ADV** | OtaAdvPayload (totalSize, totalChunks, md5Hex[32]) | 8 + 38 = **46** ≤ 250 |
| **OTA_CHUNK** | OtaChunkPayload (chunkIndex, totalChunks, data[200]) | 8 + 204 = **212** ≤ 250 |

- **LexaFullFrame** : 32 octets (voir `lexacare_protocol.h`), CRC16-CCITT sur les 30 premiers octets.
- **Limite ESP-NOW** : 250 octets ; toutes les combinaisons restent ≤ 250.

---

## Comportement Gateway (Node 0)

- Défini par `g_lexacare_this_node_is_gateway` (config) ou `nodeShortId == 0`.
- **Sortie** : pour chaque paquet **Data** reçu (et nouveau selon cache), décode LexaFullFrame, vérifie CRC16, formate en JSON et envoie une ligne sur Serial (log_dual). Clés : `nodeId`, `vBat`, `probFallLidar`, `tempExt`, `fw_ver`, etc.
- **Entrée** : `serialGatewayTask` lit le port Série pour l’OTA : d’abord 38 octets (OTA start : totalSize, totalChunks, MD5 hex), puis blocs de 200 octets (chunks). Chaque bloc est encapsulé en OTA_ADV (une fois) puis OTA_CHUNK et envoyé en broadcast.

---

## OTA par flooding (push Série → Gateway → broadcast)

1. **PC** : envoie sur Serial vers la Gateway (Node 0) :
   - **Phase start** : 38 octets binaires (4 octets totalSize LE + 2 octets totalChunks LE + 32 octets MD5 hex ASCII).
   - **Phase chunks** : blocs de 200 octets (binaire du firmware).
2. **Gateway** : construit un paquet OTA_ADV (header + OtaAdvPayload), envoie en broadcast ; pour chaque bloc de 200 octets, construit OTA_CHUNK (header + OtaChunkPayload), envoie en broadcast.
3. **Tous les nœuds** (y compris la Gateway) : reçoivent les paquets ; si `msgId` inconnu (cache), traitent OTA_ADV (→ `Update.begin`) ou OTA_CHUNK (→ `Update.write`), décrémentent TTL, appliquent un jitter 5–50 ms, retransmettent en broadcast.
4. **Dernier chunk** : `Update.end(true)`, vérification MD5 ; si OK → NVS `fw_ver`, `ESP.restart()`.

---

## Build et flash

```bash
pio run
pio run -t upload
```

- **Partitions** : `default_16MB.csv` (app0 / app1 6,4 Mo chacun).
- **Exclusion** : `mesh_handler.cpp`, `mesh_mqtt.cpp`, `ota_mesh.cpp` exclus du build (voir `platformio.ini`).

---

## Configuration (`src/config/config.h`)

- **LEXACARE_MESH_ESPNOW_FLOODING** : 1 = mesh ESP-NOW par inondation (mode actuel).
- **g_lexacare_this_node_is_gateway** : 1 = nœud passerelle (Serial JSON + réception OTA depuis PC).
- **ESPNOW_MSG_CACHE_SIZE** : 50 (cache anti-doublon).
- **ESPNOW_TTL_DEFAULT** : 10.
- **ESPNOW_JITTER_MS_MIN / MAX** : 5 / 50.
- **QUEUE_ESPNOW_RX_LEN / QUEUE_ESPNOW_TX_LEN** : 10 / 4.

---

## Vérification (Agent de Vérification)

| Point | Vérification |
|-------|---------------|
| **Cache anti-doublon** | 50 entrées × 4 octets = 200 octets + index circulaire ; buffer statique, pas de malloc, pas de fuite. |
| **Limite 250 octets** | Data 40, OTA_ADV 46, OTA_CHUNK 212 ; toutes ≤ 250. |
| **CRC16** | Appliqué à chaque LexaFullFrame (Data) ; rejet si invalide. |
| **OTA** | Validation MD5 en fin de réception ; reboot uniquement si MD5 OK et `Update.end(true)`. |
| **Mutex** | Cache msgId, état OTA et Update.write protégés par mutex/sémaphore. |
| **Jitter** | 5–50 ms avant chaque retransmission pour limiter les collisions (CSMA/CA logiciel). |
| **Partitions** | `default_16MB.csv` ; pas de dépassement 6,4 Mo par slot. |

---

## Documentation Doxygen

Commentaires Doxygen (format Javadoc) présents dans : `lexacare_protocol.h`, `espnow_mesh.h/cpp`, `ota_manager.h/cpp`, `sensor_sim.h/cpp`, `queues_events.h/cpp`, `main.cpp`. Génération optionnelle :

```bash
doxygen Doxyfile
```

---

## Fichiers principaux

| Fichier | Rôle |
|---------|------|
| `main.cpp` | Init, création des tâches (espnowRx, espnowTx, serialGateway si Gateway), loop LED + EVENT_OTA_READY. |
| `lexacare_protocol.h` | EspNowMeshHeader, LexaFullFrame, OtaAdvPayload, OtaChunkPayload, CRC16. |
| `comm/espnow_mesh.cpp` | Init WiFi/ESP-NOW, callback RX → queue, cache 50 msgId, `espnow_mesh_handle_packet()` (dispatch + retransmission). |
| `comm/ota_manager.cpp` | `ota_manager_on_ota_adv` / `ota_manager_on_ota_chunk` (Update, MD5, NVS, reboot). |
| `rtos/queues_events.cpp` | `g_queue_espnow_rx`, `g_queue_espnow_tx`, `g_system_events`. |
| `sensors/sensor_sim.cpp` | Tâche 1 Hz, génère LexaFullFrame, pousse dans `g_queue_espnow_tx`. |
