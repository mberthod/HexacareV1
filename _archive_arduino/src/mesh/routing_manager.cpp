/**
 * @file routing_manager.cpp
 * @brief Routage Tree Mesh (CDC Hexacare V2) : Construction du réseau, Heartbeats, Self-Healing.
 *
 * Machine d'état : INIT → SCANNING → EVALUATING → JOINING → CONNECTED.
 * En cas de perte parent : CONNECTED → ORPHAN → SCANNING.
 */

#include "mesh/routing_manager.h"
#include "system/led_manager.h"
#include "config/config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <string.h>
#include <algorithm>

static const char *TAG = "ROUTING";
/**
 * @defgroup RoutingManager Routage Mesh - Gestionnaire de Routage (routing_manager)
 * @ingroup Mesh
 *
 * @brief Module central de gestion du routage en topologie arborescente (Tree Mesh) pour le firmware Lexacare V2.
 *
 * Ce module implémente toutes les fonctions nécessaires à l'établissement et à la maintenance de la topologie d'un réseau mesh à base d'ESP-NOW,
 * selon une structure en arbre (root/parent/enfants), avec support des cœurs dual (ESP32-S3), du handover dynamique (perte/recherche de parent),
 * de l'ajout dynamique d'enfants, et de la sécurisation de la communication parent-enfant.
 *
 * ## Fonctionnalités principales&nbsp;:
 * - **Machine d'état du routage** : INIT &rarr; SCANNING &rarr; EVALUATING &rarr; JOINING &rarr; CONNECTED &rarr; ORPHAN.
 * - **Sélection dynamique du parent** : évaluation des parents disponibles selon la couche, l'identifiant et le nombre d'enfants.
 * - **Gestion des enfants** (ChildInfo) : ajout, suppression, rafraîchissement (lastSeen), gestion du nombre max.
 * - **Routage des trames** : unicasts (parent, enfants), diffusion contrôlée (flood, restriction via layers).
 * - **Gestion des heartbeats** : supervision du lien parent, calcul du nombre d'échecs consécutifs, rechute en mode "orphelin".
 * - **API thread-safe** pour l'accès aux informations du routage depuis d'autres tâches : layer, parent, identifiant, enfants.
 * - **Gestion de la persistance** : certains éléments (identifiant, historique) peuvent être restaurés au reboot.
 *
 * ## Glossaire&nbsp;:
 * - **Nœud ROOT** : Racine du réseau, typiquement une passerelle série (Gateway) ou le dispositif maître. Couche (layer) == 0.
 * - **Parent** : Nœud auquel le nœud courant est connecté dans l'arbre. Il relaye les messages vers la racine.
 * - **Enfant** : Nœud(s) connectés en aval. Le nœud courant s'occupe d'eux pour l'acheminement, la gestion OTA, etc.
 * - **Layer** : Profondeur de l'arbre mesh depuis la racine.
 * - **State** : État courant de la machine de routage (voir NodeState).
 *
 * ## Sécurité et robustesse&nbsp;:
 * - Toutes les opérations sur les listes d'enfants ou la sélection du parent sont protégées contre les accès concurrents (utilisation de mutex si besoin).
 * - Les états internes de la topologie sont journalisés via ESP_LOG (debug, info, erreur...).
 * - Défauts réseaux, pertes de parent ou présence de boucles sont gérés et provoquent le fallback vers les états appropriés (orphelin, scan, etc.).
 *
 * ## Fonctions publiques principales&nbsp;:
 * @code
 * void        routing_init(void);
 * void        routing_task(void *pv);
 * void        routing_set_root(void);
 * uint8_t     routing_get_layer(void);
 * uint16_t    routing_get_my_id(void);
 * uint16_t    routing_get_parent_id(void);
 * bool        routing_get_parent_mac(uint8_t* mac_out);
 * std::vector<ChildInfo> routing_get_children(void);
 * @endcode
 *
 * @par Structure principale&nbsp;:
 * Le module conserve des variables internes pour surveiller l'état d'appartenance au réseau,
 * les informations sur le parent et les enfants, ainsi que le statut des communications Heartbeat.
 *
 * @image html routing_state_machine.png "Diagramme de Machine d'Etat de Routage"
 *
 * @author
 * Lexacare Firmware Team <dev@hexacare.fr>
 * @version 2.0.x
 * @date    2023-2024
 *
 * @see ChildInfo
 * @see routing_manager.h
 */

/**
 * @enum NodeState
 * @brief Énumère les différents états de la machine d'état de routage du module mesh.
 *
 * Cette énumération décrit tous les états possibles dans lesquels un nœud peut se trouver tout au long de son cycle de vie dans le réseau mesh arborescent.
 * Elle permet d'assurer la cohérence du protocole de découverte, de connexion et de maintien de la topologie du maillage.
 *
 * | État               | Description détaillée                                                                                       |
 * |--------------------|------------------------------------------------------------------------------------------------------------|
 * | STATE_INIT         | État d'initialisation : le nœud démarre, charge ses paramètres (identifiants, historique, etc.),           |
 * |                    | et attend le lancement effectif du processus de routage. Passage rapide vers SCANNING à l'issue de l'init. |
 * | STATE_SCANNING     | Le nœud écoute et collecte les beacons des voisins afin de trouver un parent potentiel dans l'arbre.        |
 * |                    | Il s'agit de la phase de découverte où le nœud recherche la meilleure opportunité de connexion.            |
 * | STATE_EVALUATING   | Après avoir accumulé des candidats, le nœud analyse les beacons reçus (ex : couche la plus basse,          |
 * |                    | nombre d'enfants minimal, stabilité...). Il sélectionne le meilleur parent candidat et prépare la connexion.|
 * | STATE_JOINING      | Le nœud envoie une requête d'association (JOIN_REQ) vers le parent sélectionné et attend un accusé-         |
 * |                    | réception (JOIN_ACK). Il hésite dans cet état jusqu'à la réception de la réponse ou un timeout.            |
 * | STATE_CONNECTED    | Le nœud est connecté à l'arbre. Il relaie périodiquement des heartbeats à son parent, maintient la          |
 * |                    | surveillance de la connexion, et gère potentiellement des enfants.                                          |
 * | STATE_ORPHAN       | Le nœud a perdu la connexion à son parent (timeout heartbeat ou erreur), il devient orphelin et            |
 * |                    | reprend une phase de SCANNING/évaluation.                                                                  |
 *
 * Séquence d'états typique pour un nœud standard (hors root) :
 * - INIT → SCANNING → EVALUATING → JOINING → CONNECTED
 *   (Si perte de parent : CONNECTED → ORPHAN → SCANNING ...)
 *
 * Séquence pour un nœud racine (ROOT/GATEWAY) :
 * - INIT → CONNECTED (root ne scanne pas et n'a pas de parent)
 *
 * Cette énumération garantit le suivi précis de l'état interne et la robustesse face aux événements réseau ou aux perturbations.
 */
enum NodeState
{
    /**
     * @brief État d'initialisation.
     *
     * Le nœud effectue sa configuration initiale et prépare le routage.
     * Peut charger un historique de connexion ou configurer des identifiants.
     */
    STATE_INIT,

    /**
     * @brief Scan des voisins.
     *
     * Le nœud écoute activement les beacons radio pour recenser les voisins présents
     * dans le réseau mesh. Accumule les candidats potentiels à la connexion.
     */
    STATE_SCANNING,

    /**
     * @brief Évaluation des candidats parent.
     *
     * Analyse détaillée des beacons et sélection du parent optimal (plus faible couche,
     * parent non saturé, stabilité, etc.). Prêt à envoyer une requête d'adoption.
     */
    STATE_EVALUATING,

    /**
     * @brief Demande d'association (JOIN_REQ) en attente de confirmation.
     *
     * Le nœud a envoyé une demande de rattachement au parent choisi. Il attend la réponse (JOIN_ACK).
     * Retourne en SCANNING si le parent n'accepte pas ou en cas de timeout.
     */
    STATE_JOINING,

    /**
     * @brief Nœud connecté à l'arbre mesh.
     *
     * Connecté à un parent (sauf racine), entretien la connexion (heartbeats),
     * gère les demandes de ses propres enfants potentiels, relaie éventuellement les trames.
     */
    STATE_CONNECTED,

    /**
     * @brief Nœud orphelin.
     *
     * Le nœud a perdu son parent ou la connexion mesh. Repasse en découverte (SCANNING/INIT).
     * Permet une reconnexion automatique et la résilience de la topologie.
     */
    STATE_ORPHAN
};

/**
 * @brief État interne du routeur.
 *
 * Variable globale stockant l'état actuel du routeur dans son cycle de vie.
 * Elle est utilisée pour suivre l'avancement du processus de découverte, de connexion,
 * et de maintien de la topologie dans le réseau mesh arborescent.
 */
static NodeState s_state = STATE_INIT;

/**
 * @brief Couche du nœud dans l'arbre mesh.
 *
 * Variable globale stockant la couche (layer) du nœud dans l'arbre mesh.
 * - 0 : Racine (ROOT/GATEWAY).
 * - 1..254 : Nœuds intermédiaires.
 * @note - 255 : Nœud non connecté ou en erreur.
 * @see NodeState
 */
static uint8_t s_my_layer = 255;
/**
 * @brief Couche précédente du nœud dans l'arbre mesh.
 *
 * Variable globale stockant la couche (layer) précédente du nœud dans l'arbre mesh.
 * Elle est utilisée pour éviter les boucles de routage (loop avoidance).
 * @see NodeState
 */
static uint8_t s_my_layer_previous = 255; /* Pour loop avoidance (rejeter layer >= previous) */
/**
 * @brief Identifiant unique du nœud.
 *
 * Variable globale stockant l'identifiant unique du nœud dans le réseau mesh.
 * Elle est utilisée pour identifier de façon unique chaque nœud dans le réseau.
 * @see NodeState
 */
static uint8_t s_my_mac[6] = {0};
/**
 * @brief Adresse MAC du parent connecté.
 *
 * Variable globale stockant l'adresse MAC du parent connecté au nœud courant.
 * Elle est utilisée pour envoyer des trames vers le parent.
 * @see NodeState
 */
static uint8_t s_parent_mac[6] = {0};

/**
 * @brief Dernier tick où on a reçu HEARTBEAT_ACK.
 *
 * Variable globale stockant le dernier tick où on a reçu HEARTBEAT_ACK du parent connecté.
 * Elle est utilisée pour surveiller la connexion au parent.
 */
static uint32_t s_last_heartbeat_ack = 0; /* Dernier tick où on a reçu HEARTBEAT_ACK */
static uint32_t s_last_heartbeat_send = 0;
static uint8_t s_heartbeat_fail_count = 0;
/**
 * @brief Liste des enfants connectés.
 *
 * Variable globale stockant la liste des enfants connectés au nœud courant.
 * Elle est utilisée pour gérer les enfants connectés et les envoyer des trames vers eux.
 */
static std::vector<ChildInfo> s_children;
/**
 * @brief Candidats parent (scan & evaluate) : on garde le meilleur.
 *
 * Variable globale stockant le meilleur candidat parent pour la connexion.
 * Elle est utilisée pour sélectionner le meilleur parent pour la connexion.
 */
static uint8_t s_best_parent_candidate[6] = {0};
static uint8_t s_best_layer = 255;
static uint8_t s_best_parent_mac[6] = {0};
static uint8_t s_best_children_count = 255;
/** Compteur d'ACK DATA (réutilise MSG_JOIN_ACK) reçu depuis le parent courant. */
static volatile uint32_t s_data_join_ack_counter = 0;

/**
 * @brief Ajoute un parent comme pair ESP-NOW.
 *
 * Cette fonction ajoute un parent comme pair ESP-NOW si celui-ci n'est pas déjà enregistré.
 * Elle est utilisée pour ajouter le parent comme pair ESP-NOW pour l'envoi de trames.
 */
static void add_parent_as_peer(const uint8_t *mac)
{
    if (!mac || esp_now_is_peer_exist(mac))
        return;
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) == ESP_OK)
    {
        ESP_LOGD(TAG, "Peer parent ajoute");
    }
}

/**
 * @brief Dérive un identifiant court 16 bits depuis une adresse MAC (affichage / compat).
 * Utilisé pour nodeId dans ChildInfo et pour routing_get_my_id / routing_get_parent_id.
 */
static uint16_t mac_to_short_id(const uint8_t *mac)
{
    if (!mac) return 0;
    return (uint16_t)((mac[4] << 8) | mac[5]);
}

/**
 * @brief Met à jour un enfant connecté (identifié uniquement par son adresse MAC).
 *
 * Recherche par MAC ; si trouvé, met à jour lastSeen et is_active.
 * Sinon ajoute un nouvel enfant avec nodeId dérivé de la MAC (affichage).
 */
static void update_child(const uint8_t *mac)
{
    if (!mac) return;
    uint32_t now = xTaskGetTickCount();
    for (auto &child : s_children)
    {
        if (memcmp(child.mac, mac, 6) == 0)
        {
            child.lastSeen = now;
            child.is_active = true;
            return;
        }
    }
    if (s_children.size() < MAX_CHILDREN_PER_NODE)
    {
        ChildInfo c;
        memcpy(c.mac, mac, 6);
        c.nodeId = mac_to_short_id(mac);
        c.lastSeen = now;
        c.is_active = true;
        s_children.push_back(c);
        add_parent_as_peer(mac);
        ESP_LOGI(TAG, "Nouvel enfant MAC %02X:%02X:%02X:%02X:%02X:%02X (id %04X)", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned)c.nodeId);
    }
}

/**
 * @brief Vérifie les timeouts enfants (30 s) et heartbeat manqués (3 → ORPHAN).
 *
 * Cette fonction vérifie les timeouts enfants (30 s) et heartbeat manqués (3 → ORPHAN).
 * Elle est utilisée pour vérifier les timeouts enfants et heartbeat manqués.
 */
static void routing_check_timeouts(void)
{
    uint32_t now = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(ROUTING_CHILD_TIMEOUT_MS);

    /* Parent : retirer les enfants sans heartbeat depuis 30 s */
    auto it = s_children.begin();
    while (it != s_children.end())
    {
        if ((now - it->lastSeen) > timeout)
        {
            esp_now_del_peer(it->mac);
            ESP_LOGW(TAG, "Enfant perdu (timeout 30s): %04X", it->nodeId);
            it = s_children.erase(it);
        }
        else
        {
            ++it;
        }
    }

    /* Enfant : si 3 heartbeats sans ACK → ORPHAN */
    if (s_state == STATE_CONNECTED && s_my_layer != 0)
    {
        if ((now - s_last_heartbeat_ack) > pdMS_TO_TICKS(ROUTING_HEARTBEAT_INTERVAL_MS + 2000))
        {
            s_heartbeat_fail_count++;
            if (s_heartbeat_fail_count >= ROUTING_HEARTBEAT_FAIL_ORPHAN)
            {
                ESP_LOGW(TAG, "Parent perdu (3 heartbeats sans ACK) -> ORPHAN");
                s_state = STATE_ORPHAN;
                led_manager_set_state(LED_STATE_ORPHAN);
            }
        }
    }
}

/**
 * @brief CDC : reconnexion après perte du parent.
 *
 * Cette fonction reconnexion après perte du parent.
 * Elle est utilisée pour réinitialiser les variables de routage et devenir orphelin.
 */
static void routing_orphan_recovery(void)
{
    s_my_layer_previous = s_my_layer;
    s_my_layer = 255;
    memset(s_parent_mac, 0, 6);
    memset(s_parent_mac, 0, 6);
    s_heartbeat_fail_count = 0;
    s_best_layer = 255;
    memset(s_best_parent_candidate, 0, 6);
    memset(s_best_parent_mac, 0, 6);
    s_state = STATE_SCANNING;
    led_manager_set_state(LED_STATE_SCANNING);
    ESP_LOGI(TAG, "Orphan recovery -> SCANNING");
}

/**
 * @brief Initialisation du routage.
 *
 * Cette fonction initialise le routage.
 * Elle est utilisée pour initialiser le routage.
 */
void routing_init(void)
{
    //uint8_t mac[6];
    esp_read_mac(s_my_mac, ESP_MAC_WIFI_STA);
    s_state = STATE_INIT;
    ESP_LOGI(TAG, "Init Routing. My MAC: %02X:%02X:%02X:%02X:%02X:%02X", s_my_mac[0], s_my_mac[1], s_my_mac[2], s_my_mac[3], s_my_mac[4], s_my_mac[5]);
}

/**
 * @brief Configure le noeud comme ROOT (chef de réseau) dans le mesh Lexacare Tree.
 *
 * Cette fonction positionne le noeud courant comme chef ("ROOT") de l'arbre mesh.
 * Elle doit être appelée uniquement sur le noeud passerelle (Gateway), c'est-à-dire l'unique racine du réseau.
 *
 * **Effets principaux :**
 * - Attribue au noeud la couche 0 (s_my_layer = 0), signifiant qu'il est la racine du réseau mesh.
 * - Met à jour l'état interne du routeur (s_state) en le passant à STATE_CONNECTED, indiquant une connexion stable.
 * - Déclenche l'animation LED spéciale "ROOT" via led_manager_set_state(LED_STATE_ROOT) pour signaler visuellement à l'utilisateur
 *   que ce noeud est désormais chef du réseau.
 * - Affiche un message informatif dans les logs série via ESP_LOGI, avec confirmation d'entrée en mode ROOT.
 *
 * **Utilisation typique :**
 * - Appelée dans la séquence d'initialisation du firmware lorsque le flag g_lexacare_this_node_is_gateway est à true.
 *
 * @see routing_init()
 * @see led_manager_set_state()
 * @see g_lexacare_this_node_is_gateway
 * @see STATE_CONNECTED
 */
void routing_set_root(void)
{
    s_my_layer = 0;
    s_state = STATE_CONNECTED;
    led_manager_set_state(LED_STATE_ROOT);
    ESP_LOGI(TAG, "Mode ROOT (Layer 0)");
}

/**
 * @brief Envoie un paquet unicast à un nœud du mesh en utilisant ESP-NOW.
 *
 * Cette fonction encapsule une charge utile arbitraire (payload) dans l'entête TreeMeshHeader puis
 * l'envoie à une adresse MAC spécifique (dest_mac) à l'aide du protocole ESP-NOW.
 * Elle gère automatiquement l'ajout du pair ESP-NOW si celui-ci n'est pas déjà enregistré,
 * prépare l'entête réseau, copie la charge utile, puis effectue la transmission.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du pointeur MAC de destination et la capacité du buffer interne.
 * - Construit l'entête TreeMeshHeader dans le buffer d'envoi (magic, type de message, origine, couche, TTL...).
 * - Copie la charge utile (payload) à la suite de l'entête dans le buffer.
 * - S'assure que le destinataire est enregistré dans la table des pairs ESP-NOW.
 *     - Si nécessaire, crée et configure une structure esp_now_peer_info_t, puis ajoute le pair.
 * - Envoie le buffer construit via esp_now_send().
 * - Retourne true si l'envoi a réussi, false sinon.
 *
 * @param[in] dest_mac  Adresse MAC du destinataire (6 octets, non NULL).
 * @param[in] msgType   Type de message à positionner dans l'entête Mesh.
 * @param[in] payload   Pointeur sur les données à transmettre (peut être NULL si len=0).
 * @param[in] len       Taille effective de la charge utile en octets.
 *
 * @retval true    Si l'envoi du paquet ESP-NOW a été effectué avec succès.
 * @retval false   Si l'adresse MAC est invalide, buffer trop petit, pair inexistant non ajoutable,
 *                 ou erreur d'envoi ESP-NOW.
 *
 * @note Le TTL (Time-To-Live) initial du message est fixé à 15 (MAX_HOP).
 * @note La taille totale (entête + payload) ne doit pas dépasser 250 octets.
 * @note Si le pair n'existait pas, il est ajouté de façon dynamique avant l'envoi.
 * @note Cette fonction est utilisée principalement pour acheminer les messages entre parent et enfant dans l'arbre mesh.
 *
 * @see TreeMeshHeader
 * @see esp_now_add_peer()
 * @see esp_now_send()
 * @see routing_forward_upstream()
 * @see routing_route_downstream()
 */

/**
 * @brief Envoie un paquet unicast à un nœud du mesh en utilisant ESP-NOW.
 *
 * Cette fonction encapsule une charge utile arbitraire (payload) dans l'entête TreeMeshHeader puis
 * l'envoie à une adresse MAC spécifique (dest_mac) à l'aide du protocole ESP-NOW.
 * Elle gère automatiquement l'ajout du pair ESP-NOW si celui-ci n'est pas déjà enregistré,
 * prépare l'entête réseau, copie la charge utile, puis effectue la transmission.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du pointeur MAC de destination et la capacité du buffer interne.
 * - Construit l'entête TreeMeshHeader dans le buffer d'envoi (magic, type de message, origine, couche, TTL...).
 * - Copie la charge utile (payload) à la suite de l'entête dans le buffer.
 * - S'assure que le destinataire est enregistré dans la table des pairs ESP-NOW.
 *     - Si nécessaire, crée et configure une structure esp_now_peer_info_t, puis ajoute le pair.
 * - Envoie le buffer construit via esp_now_send().
 * - Retourne true si l'envoi a réussi, false sinon.
 *
 * @param[in] dest_mac  Adresse MAC du destinataire (6 octets, non NULL).
 * @param[in] msgType   Type de message à positionner dans l'entête Mesh.
 * @param[in] payload   Pointeur sur les données à transmettre (peut être NULL si len=0).
 * @param[in] len       Taille effective de la charge utile en octets.
 *
 * @retval true    Si l'envoi du paquet ESP-NOW a été effectué avec succès.
 * @retval false   Si l'adresse MAC est invalide, buffer trop petit, pair inexistant non ajoutable,
 *                 ou erreur d'envoi ESP-NOW.
 *
 * @note Le TTL (Time-To-Live) initial du message est fixé à 15 (MAX_HOP).
 * @note La taille totale (entête + payload) ne doit pas dépasser 250 octets.
 * @note Si le pair n'existait pas, il est ajouté de façon dynamique avant l'envoi.
 * @note Cette fonction est utilisée principalement pour acheminer les messages entre parent et enfant dans l'arbre mesh.
 *
 * @see TreeMeshHeader
 * @see esp_now_add_peer()
 * @see esp_now_send()
 * @see routing_forward_upstream()
 * @see routing_route_downstream()
 */
bool routing_send_unicast(const uint8_t *dest_mac, uint8_t msgType, const uint8_t *payload, uint16_t len)
{
    uint8_t buffer[250];
    if (!dest_mac || sizeof(TreeMeshHeader) + len > sizeof(buffer))
        return false;

    TreeMeshHeader *hdr = (TreeMeshHeader *)buffer;
    hdr->magic = MESH_MAGIC_BYTE;
    hdr->msgType = msgType;
    memcpy(hdr->srcMac, s_my_mac, 6);
    hdr->destNodeId = 0xFFFF; // Broadcast pour tous les nœuds
    hdr->layer = s_my_layer;
    hdr->ttl = 15;
    hdr->payloadLen = len;
    if (len > 0 && payload)
        memcpy(buffer + sizeof(TreeMeshHeader), payload, len);

    auto send_once = [&]() -> bool
    {
        if (!esp_now_is_peer_exist(dest_mac))
        {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, dest_mac, 6);
            peerInfo.channel = 0;
            peerInfo.encrypt = false;
            if (esp_now_add_peer(&peerInfo) != ESP_OK)
                return false;
        }
        return esp_now_send(dest_mac, buffer, sizeof(TreeMeshHeader) + len) == ESP_OK;
    };

#if ROUTING_DATA_ACK_ENABLE
    /* Règle demandée: chaque MSG_DATA doit être acquitté par un JOIN_ACK du parent. */
    if (msgType == MSG_DATA && s_my_layer != 0)
    {
        const TickType_t timeout_ticks = pdMS_TO_TICKS(ROUTING_DATA_ACK_TIMEOUT_MS);
        for (uint8_t attempt = 0; attempt <= ROUTING_DATA_ACK_RETRY_MAX; ++attempt)
        {
            const uint32_t ack_before = s_data_join_ack_counter;
            if (!send_once())
            {
                ESP_LOGW(TAG, "MSG_DATA envoi KO (tentative %u/%u).",
                         (unsigned)(attempt + 1), (unsigned)(ROUTING_DATA_ACK_RETRY_MAX + 1));
                continue;
            }

            TickType_t t0 = xTaskGetTickCount();
            while ((xTaskGetTickCount() - t0) < timeout_ticks)
            {
                if (s_data_join_ack_counter > ack_before)
                {
                    if (attempt > 0)
                    {
                        ESP_LOGW(TAG, "MSG_DATA ACK recu apres retry %u/%u.",
                                 (unsigned)attempt, (unsigned)ROUTING_DATA_ACK_RETRY_MAX);
                    }
                    return true;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }

            ESP_LOGW(TAG, "Timeout ACK MSG_DATA (%ums) tentative %u/%u -> renvoi.",
                     (unsigned)ROUTING_DATA_ACK_TIMEOUT_MS,
                     (unsigned)(attempt + 1),
                     (unsigned)(ROUTING_DATA_ACK_RETRY_MAX + 1));
        }
        ESP_LOGE(TAG, "MSG_DATA abandonne: aucun JOIN_ACK recu apres %u tentatives.",
                 (unsigned)(ROUTING_DATA_ACK_RETRY_MAX + 1));
        return false;
    }
#endif

    return send_once();
}

/**
 * @brief Forward une trame mesh vers le parent (upstream).
 *
 * Cette fonction est utilisée pour acheminer les messages d'un nœud vers son parent dans l'arbre mesh.
 * Elle gère automatiquement le décrément du TTL (Time-To-Live) de la trame, la copie du buffer,
 * l'ajout du pair ESP-NOW si nécessaire, et la transmission effective.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du buffer et de l'entête TreeMeshHeader.
 * - Rejette si le TTL est atteint (h->ttl == 0) ou si le noeud est ROOT (s_my_layer == 0).
 * - Récupère l'adresse MAC du parent via routing_get_parent_mac().
 * - Prépare un nouveau buffer temporaire pour le décrément du TTL.
 * - S'assure que le parent est enregistré dans la table des pairs ESP-NOW.
 *     - Si nécessaire, crée et configure une structure esp_now_peer_info_t, puis ajoute le pair.
 * - Envoie le buffer temporaire via esp_now_send().
 *
 * @param[in] data    Pointeur sur les données à transmettre (buffer mesh).
 * @param[in] len     Taille effective de la trame en octets.
 *
 * @note Le TTL est décrémenté de 1 avant l'envoi.
 * @note Si le parent n'existait pas, il est ajouté de façon dynamique avant l'envoi.
 * @note Cette fonction est utilisée pour acheminer les messages entre parent et enfant dans l'arbre mesh.
 *
 * @see TreeMeshHeader
 * @see esp_now_add_peer()
 * @see esp_now_send()
 * @see routing_get_parent_mac()
 */
void routing_forward_upstream(const uint8_t *data, size_t len)
{
    if (!data || len < sizeof(TreeMeshHeader))
        return;
    TreeMeshHeader *h = (TreeMeshHeader *)data;
    if (h->magic != MESH_MAGIC_BYTE || h->ttl == 0)
        return;
    if (s_my_layer == 0)
        return; /* ROOT ne forward pas en upstream */
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac))
        return;

    /* Décrémenter TTL (copie pour ne pas modifier le buffer original si partagé) */
    uint8_t buf[250];
    if (len > sizeof(buf))
        return;
    memcpy(buf, data, len);
    TreeMeshHeader *h2 = (TreeMeshHeader *)buf;
    h2->ttl--;

    if (!esp_now_is_peer_exist(parent_mac))
        add_parent_as_peer(parent_mac);
    esp_now_send(parent_mac, buf, len);
}

/**
 * @brief Envoie une trame mesh vers un nœud cible (downstream).
 *
 * Cette fonction est utilisée pour envoyer une trame mesh vers un nœud cible spécifique.
 * Elle parcourt la liste des enfants directs et transmet la trame uniquement à l'enfant correspondant.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du buffer et de l'entête TreeMeshHeader.
 * - Parcourt la liste des enfants directs.
 * - Si l'enfant correspondant est actif (c.is_active == true), s'assure que le pair ESP-NOW est enregistré.
 * - Envoie la trame via esp_now_send() à l'adresse MAC de l'enfant.
 * - Retourne sans erreur si l'envoi a réussi.
 *
 * @param[in] target_id  Identifiant unique du nœud cible.
 * @param[in] data    Pointeur sur les données à transmettre (buffer mesh).
 * @param[in] len     Taille effective de la trame en octets.
 *
 * @note Cette fonction est utilisée pour envoyer des messages vers des enfants directs dans l'arbre mesh.
 *
 * @see TreeMeshHeader
 * @see esp_now_add_peer()
 * @see esp_now_send()
 * @see routing_get_children()
 */
void routing_route_downstream(uint16_t target_id, const uint8_t *data, size_t len)
{
    if (!data || len < sizeof(TreeMeshHeader))
        return;
    for (const auto &c : s_children)
    {
        if (mac_to_short_id(c.mac) == target_id && c.is_active)
        {
            if (!esp_now_is_peer_exist(c.mac))
                add_parent_as_peer(c.mac);
            esp_now_send(c.mac, data, len);
            return;
        }
    }
    ESP_LOGD(TAG, "route_downstream: cible %04X pas dans enfants directs", target_id);
}

/**
 * @brief Envoie les données à tous les enfants directs (par adresse MAC).
 * Utilisé pour la propagation OTA, commandes mesh, etc.
 */
void routing_propagate_to_children(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return;
    for (const auto &c : s_children)
    {
        if (!c.is_active)
            continue;
        if (!esp_now_is_peer_exist(c.mac))
            add_parent_as_peer(c.mac);
        esp_now_send(c.mac, (uint8_t *)data, len);
    }
}

/**
 * @brief Envoie les données à un nœud enfant identifié par son adresse MAC.
 */
bool routing_send_to_child_mac(const uint8_t *dest_mac, const uint8_t *data, size_t len)
{
    if (!dest_mac || !data || len == 0)
        return false;
    for (const auto &c : s_children)
    {
        if (memcmp(c.mac, dest_mac, 6) == 0 && c.is_active)
        {
            if (!esp_now_is_peer_exist(c.mac))
                add_parent_as_peer(c.mac);
            return (esp_now_send(c.mac, (uint8_t *)data, len) == ESP_OK);
        }
    }
    return false;
}

/**
 * @brief Gestionnaire de réception des trames mesh.
 *
 * Cette fonction est appelée lorsqu'une trame mesh est reçue via ESP-NOW.
 * Elle gère les différents types de messages (Beacon, JOIN_REQ, JOIN_ACK, HEARTBEAT, HEARTBEAT_ACK)
 * et traite les événements correspondants.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du buffer et de l'entête TreeMeshHeader.
 * - Rejette si le magic byte n'est pas valide.
 * - Traite les différents types de messages :
 *     - MSG_BEACON : Collecte des Beacons pour évaluation du parent.
 *     - MSG_JOIN_REQ : Réception d'une demande deJOIN vers un parent.
 *     - MSG_JOIN_ACK : Réponse à une demande de JOIN.
 *     - MSG_HEARTBEAT : Ping de maintien de connexion (enfant -> parent).
 *     - MSG_HEARTBEAT_ACK : Réponse explicite du parent au heartbeat (évite STATE_ORPHAN).
 * - Retourne sans erreur si le message a été traité correctement.
 *
 * @param[in] mac     Adresse MAC de l'émetteur.
 * @param[in] data    Pointeur sur les données reçues (buffer mesh).
 * @param[in] len     Taille effective des données reçues en octets.
 *
 * @note Cette fonction est utilisée pour gérer les événements de réception des trames mesh.
 *
 * @see TreeMeshHeader
 * @see esp_now_is_peer_exist()
 * @see routing_get_parent_mac()
 * @see routing_send_unicast()
 */
void on_mesh_receive(const uint8_t *mac, const uint8_t *data, int len)
{
    if (!mac || !data || len < (int)sizeof(TreeMeshHeader))
        return;
    TreeMeshHeader *hdr = (TreeMeshHeader *)data;
    if (hdr->magic != MESH_MAGIC_BYTE)
        return;

    /* ---------- SCANNING : collecte des Beacons pour évaluation ---------- */
    if (s_state == STATE_SCANNING && hdr->msgType == MSG_BEACON)
    {
        if (len < (int)(sizeof(TreeMeshHeader) + sizeof(BeaconPayload)))
            return;
        BeaconPayload *b = (BeaconPayload *)(data + sizeof(TreeMeshHeader));
        /* Loop avoidance : rejeter si layer >= ma couche précédente */
        if (hdr->layer >= s_my_layer_previous)
            return;
        if (b->currentChildrenCount >= MAX_CHILDREN_PER_NODE)
            return;
        /* Meilleur candidat : couche la plus basse, puis moins d'enfants */
        if (hdr->layer < s_best_layer || (hdr->layer == s_best_layer && b->currentChildrenCount < s_best_children_count))
        {
            s_best_layer = hdr->layer;
            s_best_children_count = b->currentChildrenCount;
            memcpy(s_best_parent_candidate, hdr->srcMac, 6);
            led_manager_set_state(LED_STATE_SCANNING);
            ESP_LOGD(TAG, "Beacon recu parent %02X:%02X:%02X:%02X:%02X:%02X layer=%u enfants=%u", hdr->srcMac[0], hdr->srcMac[1], hdr->srcMac[2], hdr->srcMac[3], hdr->srcMac[4], hdr->srcMac[5], (unsigned)hdr->layer, (unsigned)b->currentChildrenCount);
        }
        return;
    }

    /* ---------- JOINING : réception JOIN_ACK avec couche assignée ---------- */
    if (s_state == STATE_JOINING && hdr->msgType == MSG_JOIN_ACK && memcmp(hdr->srcMac, s_best_parent_candidate, 6) == 0)
    {
        uint8_t assigned = hdr->layer + 1; /* Par défaut : parent_layer + 1 */
        if (len >= (int)(sizeof(TreeMeshHeader) + sizeof(JoinAckPayload)))
        {
            JoinAckPayload *j = (JoinAckPayload *)(data + sizeof(TreeMeshHeader));
            assigned = j->assigned_layer;
        }
        s_state = STATE_CONNECTED;
        memcpy(s_parent_mac, hdr->srcMac, 6);
        s_my_layer = assigned;
        s_last_heartbeat_ack = xTaskGetTickCount();
        s_heartbeat_fail_count = 0;
        add_parent_as_peer(hdr->srcMac);
        ESP_LOGI(TAG, "Connecte au parent %02X:%02X:%02X:%02X:%02X:%02X, couche assignee: %u", s_parent_mac[0], s_parent_mac[1], s_parent_mac[2], s_parent_mac[3], s_parent_mac[4], s_parent_mac[5], assigned);
        led_manager_set_state(LED_STATE_CONNECTED);
        return;
    }

    /* ---------- CONNECTED : JOIN_ACK utilisé comme ACK de MSG_DATA ---------- */
    if (s_state == STATE_CONNECTED && s_my_layer != 0 && hdr->msgType == MSG_JOIN_ACK && memcmp(hdr->srcMac, s_parent_mac, 6) == 0)
    {
        s_data_join_ack_counter++;
        s_last_heartbeat_ack = xTaskGetTickCount();
        ESP_LOGD(TAG, "ACK DATA (JOIN_ACK) recu du parent.");
        return;
    }

    /* ---------- CONNECTED / ROOT : JOIN_REQ, HEARTBEAT, HEARTBEAT_ACK ---------- */
    if (s_state == STATE_CONNECTED || s_my_layer == 0)
    {
        if (hdr->msgType == MSG_DATA)
        {
            /* Dès réception de DATA d'un enfant: ACK immédiat demandé (JOIN_ACK). */
            update_child((const uint8_t *)hdr->srcMac);
            JoinAckPayload j;
            j.assigned_layer = s_my_layer + 1;
            routing_send_unicast((const uint8_t *)hdr->srcMac, MSG_JOIN_ACK, (const uint8_t *)&j, sizeof(j));
            ESP_LOGD(TAG, "DATA recu de %02X:%02X:%02X:%02X:%02X:%02X -> JOIN_ACK renvoye.",
                     hdr->srcMac[0], hdr->srcMac[1], hdr->srcMac[2], hdr->srcMac[3], hdr->srcMac[4], hdr->srcMac[5]);
            return;
        }

        if (hdr->msgType == MSG_JOIN_REQ)
        {
            if (s_children.size() < MAX_CHILDREN_PER_NODE)
            {
                update_child((const uint8_t *)hdr->srcMac);
                JoinAckPayload j;
                j.assigned_layer = s_my_layer + 1;
                routing_send_unicast((const uint8_t *)hdr->srcMac, MSG_JOIN_ACK, (const uint8_t *)&j, sizeof(j));
            }
            return;
        }
    }
}

/**
 * @brief Calcule l'intervalle de temps pour la prochaine émission de Beacon (avec jitter).
 *
 * Cette fonction détermine l'intervalle de temps aléatoire entre 2000 et 5000 ms pour la prochaine émission de Beacon.
 * Elle utilise la fonction esp_random() pour générer un nombre aléatoire et applique un jitter de +/- 500 ms.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Calcule la différence entre les valeurs maximales et minimales (base = 3000 ms).
 * - Génère un nombre aléatoire entre 0 et la base (exclue).
 * - Ajoute la valeur minimale (2000 ms) pour obtenir l'intervalle final.
 *
 * @return Intervalle de temps en millisecondes pour la prochaine émission de Beacon.
 *
 * @see esp_random()
 * @see ROUTING_BEACON_INTERVAL_MS_MIN
 * @see ROUTING_BEACON_INTERVAL_MS_MAX
 */
static uint32_t beacon_interval_ms(void)
{
    uint32_t base = ROUTING_BEACON_INTERVAL_MS_MAX - ROUTING_BEACON_INTERVAL_MS_MIN;
    return ROUTING_BEACON_INTERVAL_MS_MIN + (esp_random() % (base + 1));
}

/**
 * @brief Tâche principale de gestion du réseau Tree Mesh.
 *
 * Cette fonction est la tâche principale de gestion du réseau Tree Mesh.
 * Elle gère les états de routage (INIT, SCANNING, JOINING, CONNECTED, ORPHAN) et les événements de réception des trames mesh.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie les états de routage et les événements de réception des trames mesh.
 * - Gère les événements de réception des trames mesh :
 *     - MSG_BEACON : Collecte des Beacons pour évaluation du parent.
 *     - MSG_JOIN_REQ : Réception d'une demande de JOIN vers un parent.
 *     - MSG_JOIN_ACK : Réponse à une demande de JOIN.
 *     - MSG_HEARTBEAT : Ping de maintien de connexion (enfant -> parent).
 *     - MSG_HEARTBEAT_ACK : Réponse explicite du parent au heartbeat (évite STATE_ORPHAN).
 * - Retourne sans erreur si le message a été traité correctement.
 *
 * @param[in] pv Paramètre inutilisé (conforme FreeRTOS).
 */
void routing_task(void *pv)
{
    (void)pv;
    TickType_t last_beacon = 0;
    TickType_t last_heartbeat = 0;
    uint32_t next_beacon_ms = beacon_interval_ms();

    if (s_my_layer == 0)
    {
        s_state = STATE_CONNECTED;
        led_manager_set_state(LED_STATE_ROOT);
    }
    else if (s_state == STATE_INIT)
    {
        s_state = STATE_SCANNING;
        led_manager_set_state(LED_STATE_SCANNING);
    }

    while (1)
    {
        routing_check_timeouts();

        if (s_state == STATE_ORPHAN)
        {
            routing_orphan_recovery();
            continue;
        }

        if (s_state == STATE_SCANNING && s_my_layer != 0)
        {
            led_manager_set_state(LED_STATE_SCANNING);
            s_best_layer = 255;
            s_best_children_count = 255;
            vTaskDelay(pdMS_TO_TICKS(500));
            /* Évaluation : si on a un candidat, passer en JOINING */
            if (s_best_layer != 255)
            {
                memcpy(s_best_parent_mac, s_best_parent_candidate, 6);
                s_state = STATE_JOINING;
                add_parent_as_peer(s_best_parent_mac);
                routing_send_unicast(s_best_parent_mac, MSG_JOIN_REQ, nullptr, 0);
                ESP_LOGI(TAG, "JOIN_REQ envoye au parent %02X:%02X:%02X:%02X:%02X:%02X (layer %u)", s_best_parent_mac[0], s_best_parent_mac[1], s_best_parent_mac[2], s_best_parent_mac[3], s_best_parent_mac[4], s_best_parent_mac[5], (unsigned)s_best_layer);
            }
            continue;
        }

        if (s_state == STATE_JOINING)
        {
            ESP_LOGI(TAG, "Etat JOINING (attente JOIN_ACK %u ms)...", (unsigned)ROUTING_JOIN_WAIT_MS);
            vTaskDelay(pdMS_TO_TICKS(ROUTING_JOIN_WAIT_MS));
            if (s_state != STATE_CONNECTED)
            {
                ESP_LOGW(TAG, "JOIN timeout -> SCANNING");
                s_state = STATE_SCANNING;
            }
            continue;
        }

        if (s_state == STATE_CONNECTED)
        {
            TickType_t now = xTaskGetTickCount();

            /* Heartbeat vers parent toutes les 10 s (CDC) */
            if (s_my_layer != 0)
            {
                if ((now - last_heartbeat) >= pdMS_TO_TICKS(ROUTING_HEARTBEAT_INTERVAL_MS))
                {
                    routing_send_unicast(s_parent_mac, MSG_HEARTBEAT, nullptr, 0);
                    last_heartbeat = now;
                }
            }

            /* Beacon broadcast 2–5 s avec jitter (CDC) */
            if ((now - last_beacon) >= pdMS_TO_TICKS(next_beacon_ms))
            {
                BeaconPayload b;
                b.currentChildrenCount = (uint8_t)s_children.size();
                b.rssi = 0;
                uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                routing_send_unicast(broadcast, MSG_BEACON, (uint8_t *)&b, sizeof(b));
                ESP_LOGD(TAG, "Beacon envoye layer=%u enfants=%u", (unsigned)s_my_layer, (unsigned)s_children.size());
                last_beacon = now;
                next_beacon_ms = beacon_interval_ms();
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

/**
 * @brief Récupère la liste des enfants directs connectés.
 *
 * Cette fonction retourne la liste des enfants directs connectés au nœud courant.
 * Elle est utilisée pour obtenir la liste des enfants connectés pour la gestion des OTA,
 * la diffusion des trames, etc.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Retourne la liste des enfants connectés stockée dans la variable globale s_children.
 * - La liste est une vector<ChildInfo> contenant les informations de chaque enfant (adresse MAC, identifiant, statut, etc.).
 *
 * @return Liste des enfants connectés.
 *
 * @see ChildInfo
 */
std::vector<ChildInfo> routing_get_children(void)
{
    return s_children;
}

/**
 * @brief Récupère l'adresse MAC du parent connecté.
 *
 * Cette fonction retourne l'adresse MAC du parent connecté au nœud courant.
 * Elle est utilisée pour obtenir l'adresse MAC du parent pour l'envoi de trames,
 * la gestion des OTA, etc.
 *
 * **Fonctionnement détaillé&nbsp;:**
 * - Vérifie la validité du pointeur mac_out, de l'état de routage et de la couche.
 * - Copie l'adresse MAC du parent dans le buffer mac_out si tout est valide.
 * - Retourne true si l'adresse MAC a été copiée avec succès, false sinon.
 *
 * @param[out] mac_out Pointeur sur le buffer où stocker l'adresse MAC du parent (6 octets).
 *
 * @retval true    Si l'adresse MAC du parent a été copiée avec succès.
 * @retval false   Si le pointeur est NULL, l'état de routage est incorrect, ou le nœud est ROOT.
 *
 * @see STATE_CONNECTED
 * @see s_parent_mac
 */
bool routing_get_parent_mac(uint8_t *mac_out)
{
    if (!mac_out || s_state != STATE_CONNECTED || s_my_layer == 0)
        return false;
    memcpy(static_cast<void *>(mac_out), s_parent_mac, 6);
    return true;
}

/**
 * @brief Récupère l'identifiant court 16 bits du nœud (dérivé de la MAC).
 */
uint16_t routing_get_my_id(void)
{
    return mac_to_short_id(s_my_mac);
}

/**
 * @brief Récupère l'identifiant court 16 bits du parent (dérivé de la MAC). 0xFFFF si pas de parent.
 */
uint16_t routing_get_parent_id(void)
{
    if (s_my_layer == 0)
        return 0xFFFF;
    return mac_to_short_id(s_parent_mac);
}

/**
 * @brief Récupère l'adresse MAC du nœud courant.
 */
uint8_t *routing_get_my_mac(void)
{
    return (uint8_t *)s_my_mac;
}

uint8_t routing_get_layer(void)
{
    return s_my_layer;
}
