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
    MSG_DATA = 0x20         // Données capteurs (remontent vers le ROOT)
    // OTA géré par espnow_ota (librairie officielle), plus de types OTA custom ici
};
 
 // En-tête de routage (10 octets)
 struct __attribute__((packed)) TreeMeshHeader {
     uint8_t  magic;         // MESH_MAGIC_BYTE
     uint8_t  msgType;       // MeshMsgType
     uint8_t  srcMac[6];     // Adresse MAC de l'émetteur d'origine
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

/** Taille en-tête OTA UART (totalSize 4 + totalChunks 2 + md5Hex 32 = 38). */
#define OTA_ADV_PAYLOAD_SIZE  38
/** Taille d'un chunk OTA (octets). */
#define OTA_CHUNK_DATA_SIZE  200

// Payload de données (remplace LexaFullFrame pour inclure la topologie)
 struct __attribute__((packed)) DataPayload {
     uint16_t parentId;       // Qui est mon parent actuel (pour le monitoring Python)
     uint16_t vBat;
     uint8_t  heartRate;
     uint8_t  probFall;
     int16_t  tempExt;
     // ... autres données capteurs
 };
 

/**
 * @struct RoutingTable 
 * @brief Contient l'identifiant du parent, la liste des enfants et leur nombre.
 */
struct __attribute__((packed)) RoutingTable {
    uint16_t parentId;       ///< ID du parent actuel
    uint16_t childrenId[20];  ///< IDs des enfants (max 8 enfants)
    uint8_t numChildren;     ///< Nombre d'enfants effectifs
};

#define ROUTING_TABLE_SIZE (sizeof(struct RoutingTable))  /* 2+20+1 = 23 */ // 2 octets pour le parentId, 20 octets pour les enfants, 1 octet pour le nombre d'enfants

 #endif
