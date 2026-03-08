/**
 * @file mesh_tree_protocol.h
 * @brief Le Code de la Route (Règles de Circulation).
 * 
 * @details
 * Ce fichier définit les panneaux de signalisation du réseau.
 * Il liste tous les types de messages que les boîtiers peuvent s'échanger pour gérer le trafic.
 * 
 * Les principaux panneaux (Messages) sont :
 * - **MSG_BEACON ("Je suis là")** : Un chef crie "Je suis là, j'ai de la place !" pour que les autres le trouvent.
 * - **MSG_JOIN_REQ ("Je peux venir ?")** : Un soldat demande à un chef s'il peut rejoindre son équipe.
 * - **MSG_HEARTBEAT ("Toujours vivant")** : Un petit "ping" régulier pour dire qu'on n'est pas tombé en panne.
 * - **MSG_DATA ("Voici des infos")** : Le transport des données capteurs vers le PC.
 * - **MSG_OTA_... ("Mise à jour")** : Toute une famille de messages pour gérer le téléchargement du nouveau logiciel.
 */

 #ifndef MESH_TREE_PROTOCOL_H
 #define MESH_TREE_PROTOCOL_H
 
 #include <stdint.h>
 
 #define MESH_MAGIC_BYTE 0xAB
 #define MAX_CHILDREN_PER_NODE 20
 
 // Types de messages réseau
 enum MeshMsgType {
     MSG_BEACON = 0x10,      // Diffusé par les parents pour annoncer leur présence
     MSG_JOIN_REQ = 0x11,    // Demande d'un orphelin pour rejoindre un parent
     MSG_JOIN_ACK = 0x12,    // Acceptation du parent
     MSG_HEARTBEAT = 0x13,   // Ping de maintien de connexion (enfant -> parent)
    MSG_HEARTBEAT_ACK = 0x14, // Réponse du parent au heartbeat (évite STATE_ORPHAN)
    MSG_DATA = 0x20,        // Données capteurs (remontent vers le ROOT)
    MSG_OTA_ADV = 0x30,     // Annonce de mise à jour (descend vers les enfants)
    MSG_OTA_REQ = 0x32,     // Demande d'un chunk par un enfant (montant)
    MSG_OTA_CHUNK = 0x31,   // Bloc de mise à jour (descend vers les enfants)
    MSG_OTA_DONE = 0x33     // Signal que l'enfant a fini sa mise à jour
};
 
 // En-tête de routage (10 octets)
 struct __attribute__((packed)) TreeMeshHeader {
     uint8_t  magic;         // MESH_MAGIC_BYTE
     uint8_t  msgType;       // MeshMsgType
     uint16_t srcNodeId;     // ID de l'émetteur d'origine (fin de la MAC)
     uint16_t destNodeId;    // ID de la destination (0x0000 = ROOT, 0xFFFF = Broadcast)
     uint8_t  layer;         // Couche réseau (0 = ROOT, 1 = connecté au root, etc.)
     uint8_t  ttl;           // Time To Live
     uint16_t payloadLen;    // Taille de la suite
 };
 
 // Payload d'un Beacon (diffusé périodiquement par les nœuds connectés)
struct __attribute__((packed)) BeaconPayload {
    uint8_t currentChildrenCount; // Pour que l'orphelin choisisse le parent le moins chargé
    int8_t  rssi;                 // Réservé pour calcul (ex: -80 = bon)
};

// Payload de JOIN_ACK (parent envoie la couche assignée à l'enfant)
struct __attribute__((packed)) JoinAckPayload {
    uint8_t assigned_layer; // Couche assignée (parent_layer + 1)
};

/** Payload pour demander un chunk OTA (MSG_OTA_REQ) - mécanisme PULL par index. */
struct __attribute__((packed)) OtaReqPayload {
    uint16_t requested_chunk_index;  ///< Index du chunk demandé (0-based)
};

/**
 * @struct OtaAdvPayload
 * @brief Annonce OTA (taille, nombre de chunks, MD5). Suit TreeMeshHeader quand msgType == MSG_OTA_ADV.
 */
struct __attribute__((packed)) OtaAdvPayload {
    uint32_t totalSize;      ///< Taille totale du binaire (octets)
    uint16_t totalChunks;    ///< Nombre total de blocs (200 octets chacun)
    uint8_t  md5Hex[32];     ///< MD5 du binaire en hex ASCII (32 caractères)
};

#define OTA_ADV_PAYLOAD_SIZE  (sizeof(struct OtaAdvPayload))  /* 4+2+32 = 38 */

/**
 * @struct OtaChunkPayload
 * @brief Bloc OTA (chunk_index + chunk_size + 200 octets). Suit TreeMeshHeader quand msgType == MSG_OTA_CHUNK.
 */
struct __attribute__((packed)) OtaChunkPayload {
    uint16_t chunk_index;    ///< Numéro du bloc (0-based)
    uint8_t  chunk_size;     ///< Nombre d'octets valides dans data[] (généralement 200)
    uint8_t  data[200];      ///< Données du firmware
};

#define OTA_CHUNK_PAYLOAD_SIZE (sizeof(struct OtaChunkPayload))  /* 2+1+200 = 203 */
#define OTA_CHUNK_DATA_SIZE    200

// Payload de données (remplace LexaFullFrame pour inclure la topologie)
 struct __attribute__((packed)) DataPayload {
     uint16_t parentId;       // Qui est mon parent actuel (pour le monitoring Python)
     uint16_t vBat;
     uint8_t  heartRate;
     uint8_t  probFall;
     int16_t  tempExt;
     // ... autres données capteurs
 };
 
 #endif