"""
@file lexacare_monitor.py
@brief Console GUI de gestion pour le monitoring mesh et l'OTA via port série ou MQTT.

Fonctionnement avec le firmware Lexacare V2 (ROOT) :
- Données mesh : le ROOT envoie une ligne JSON par trame reçue (nodeId, parentId, layer,
  vBat, probFallLidar, tempExt, fw_ver, heartRate, humidity, etc.).
- Mise à jour locale OTA (0x01) : flux très rapide, port série uniquement. Au démarrage le firmware
  stoppe toutes les tâches (routage, envoi, capteurs) ; seule la réception OTA tourne. À la fin :
  vérification puis reboot sur le nouveau firmware. LED violette pendant la réception.
- OTA Mesh (0x02) : "Lancer OTA Mesh" envoie 0x02 + en-tête 38 octets + chunks 200 octets.
  Le ROOT écrit en flash puis appelle ota_tree_start_propagation() : il envoie MSG_OTA_ADV
  à ses enfants. Les enfants demandent les chunks (MSG_OTA_REQ) et reçoivent MSG_OTA_CHUNK
  (mécanisme PULL, timeout 500 ms). LED bleue sur le ROOT pendant la réception/diffusion.
- Voir les nœuds en OTA : 1) LED sur chaque carte (ROOT violet/bleu, enfants orange puis 4× vert).
  2) Dans l’onglet Logs série, cocher le tag OTA pour afficher les messages OTA_TREE (ex. "Envoi ADV
  vers enfant...", "Enfant X a fini sa MAJ").
- OTA (MQTT) : optionnel ; le firmware actuel n'écoute pas MQTT pour l'OTA.

INSTALLATION DES DÉPENDANCES :
  pip install paho-mqtt pyserial

Sous Linux (Ubuntu/Debian), si tkinter manque :
  sudo apt-get install python3-tk
"""

import json
import re
import time
import threading
import queue
import struct
import hashlib
import os
import http.server
import socketserver
import paho.mqtt.client as mqtt
import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# Format OTA série (aligné firmware Hexacare V2 - serial_gateway + mesh_tree_protocol.h)
# Protocole: [1 octet mode] [38 octets header] [N × 200 octets chunks]
#   mode: 0x01 = OTA Série (flash ROOT seul, LED violette), 0x02 = OTA Mesh (ROOT diffuse, LED bleue)
#   header: totalSize (4 octets LE) + totalChunks (2 octets LE) + MD5 hex ASCII (32 octets)
#   chunks: données .bin, dernier bloc complété à 200 octets (0xFF)
# Côté mesh (ESP32↔ESP32) : ADV/REQ/CHUNK avec requested_chunk_index, chunk_index, chunk_size (firmware).
OTA_ADV_HEADER_SIZE = 38   # 4 + 2 + 32 = OTA_ADV_PAYLOAD_SIZE firmware
OTA_CHUNK_SIZE = 200       # OTA_CHUNK_DATA_SIZE firmware
OTA_MODE_SERIAL_ROOT = 0x01  # OTA Série : ROOT seul
OTA_MODE_MESH = 0x02         # OTA Mesh : ROOT puis propagation PULL vers enfants

# --- CONFIGURATION PAR DÉFAUT ---
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
TOPIC_DATA = "lexacare/mesh/data"
TOPIC_OTA = "lexacare/mesh/ota"
HTTP_PORT = 8000
PC_IP = "192.168.1.15"  # À adapter avec ipconfig / ifconfig
BAUD_SERIAL = 921600


class FirmwareServer(threading.Thread):
    """Serveur HTTP pour distribution firmware (optionnel, ex. OTA par URL future)."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.httpd = None

    def run(self):
        handler = http.server.SimpleHTTPRequestHandler
        try:
            with socketserver.TCPServer(("", self.port), handler) as self.httpd:
                self.httpd.serve_forever()
        except Exception as e:
            print(f"Erreur Serveur HTTP: {e}")


class SerialReader(threading.Thread):
    """Thread de lecture du port série (une ligne = un JSON de trame mesh ou log)."""

    def __init__(self, port, baudrate, callback, log_callback, serial_log_callback=None, topology_callback=None, ota_mesh_ack_queue=None):
        super().__init__(daemon=True)
        self.port = port
        self.baudrate = baudrate
        self.callback = callback
        self.log_callback = log_callback
        self.serial_log_callback = serial_log_callback
        self.topology_callback = topology_callback
        self.ota_mesh_ack_queue = ota_mesh_ack_queue  # queue pour [OTA_MESH] CHUNK_PROPAGATED n (index)
        self.running = True
        self.ser = None

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            self.log_callback(f"Connecté à {self.port} @ {self.baudrate} bauds.")
            if self.serial_log_callback:
                self.serial_log_callback(f"[SCRIPT] Connecté au port {self.port} @ {self.baudrate} bauds.\n")
            while self.running:
                if self.ser.in_waiting:
                    line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                    if line:
                        if self.serial_log_callback:
                            self.serial_log_callback(line + "\n")
                        if "[OTA_MESH] CHUNK_PROPAGATED" in line and self.ota_mesh_ack_queue is not None:
                            m = re.search(r"CHUNK_PROPAGATED\s+(\d+)", line)
                            if m:
                                try:
                                    self.ota_mesh_ack_queue.put_nowait(int(m.group(1)))
                                except queue.Full:
                                    pass
                        if line.startswith("[TOPOLOGY]"):
                            try:
                                json_str = line[10:].strip()
                                if json_str and self.topology_callback:
                                    self.topology_callback(json_str)
                            except Exception:
                                pass
                        elif "{" in line and "}" in line:
                            # Extraire le JSON (ligne pure ou préfixe type "[LEXACARE] ...")
                            start = line.index("{")
                            end = line.rindex("}") + 1
                            json_str = line[start:end]
                            if json_str:
                                self.callback(json_str)
        except Exception as e:
            self.log_callback(f"Erreur Série: {e}")
            if self.serial_log_callback:
                self.serial_log_callback(f"[SCRIPT] Erreur série: {e}\n")
        finally:
            if self.ser and self.ser.is_open:
                self.ser.close()

    def send(self, data):
        """Envoie une chaîne sur le port série (suffixée par \\n)."""
        if self.ser and self.ser.is_open:
            self.ser.write((data + "\n").encode("utf-8"))

    def send_raw(self, data):
        """Envoie des octets bruts sur le port série (sans suffixe)."""
        if self.ser and self.ser.is_open and isinstance(data, (bytes, bytearray)):
            self.ser.write(data)
            self.ser.flush()


class LexacareApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Lexacare V2 - Console (Série + Tree Mesh)")
        self.root.geometry("1200x750")
        self.root.configure(bg="#f0f2f5")

        self.nodes = {}
        self.topology = {}
        self.selected_file = tk.StringVar(value="Aucun fichier sélectionné")
        self.full_path = None
        self.serial_thread = None
        self.logs_paused = False
        self.ota_mesh_ack_queue = queue.Queue()
        # Tags pour filtrage du log série (ROUTING = logs ESP-IDF "ROUTING:", TOPOLOGY = lignes [TOPOLOGY])
        self.LOG_SERIAL_TAG_IDS = ["BOOT", "SERIE", "OTA", "MESH", "ROUTING", "TOPOLOGY", "MAIN", "TASK", "ERREUR", "SCRIPT", "AUTRE"]
        self.log_serial_tag_vars = {}  # id -> tk.BooleanVar
        self.log_serial_show_all_var = tk.BooleanVar(value=False)

        self.setup_ui()
        self.setup_mqtt()

        self.server = FirmwareServer(HTTP_PORT)
        self.server.start()
        self.log(f"Système prêt. Serveur HTTP (optionnel) sur port {HTTP_PORT}.")

    def setup_ui(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview", rowheight=28, font=("Segoe UI", 10))
        style.configure("Treeview.Heading", font=("Segoe UI", 11, "bold"))

        # --- Barre connexion Série ---
        conn_frame = tk.Frame(self.root, bg="#ffffff", height=44)
        conn_frame.pack(side="top", fill="x", padx=10, pady=5)

        tk.Label(conn_frame, text="Port Série:", bg="#ffffff").pack(side="left", padx=5)
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.combo_ports = ttk.Combobox(conn_frame, values=ports, width=20)
        if ports:
            self.combo_ports.set(ports[0])
        self.combo_ports.pack(side="left", padx=5)

        tk.Button(conn_frame, text="Rafraîchir", command=self.refresh_ports).pack(side="left", padx=5)
        self.btn_serial = tk.Button(
            conn_frame, text="Connecter Série", command=self.toggle_serial, bg="#e8f0fe"
        )
        self.btn_serial.pack(side="left", padx=5)

        self.lbl_mqtt = tk.Label(
            conn_frame, text="MQTT: Déconnecté", fg="red", bg="#ffffff", font=("Segoe UI", 9, "bold")
        )
        self.lbl_mqtt.pack(side="right", padx=10)

        # --- Tableau des nœuds (données mesh) + Architecture réseau ---
        table_frame = tk.Frame(self.root, bg="#f0f2f5")
        table_frame.pack(fill="both", expand=True, padx=10, pady=5)

        columns = ("id", "seen", "parent", "layer", "vbat", "fall", "temp", "hr", "ver")
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings")
        self.tree.heading("id", text="Node ID")
        self.tree.heading("seen", text="Dernière vue")
        self.tree.heading("parent", text="Parent")
        self.tree.heading("layer", text="Couche")
        self.tree.heading("vbat", text="Batterie (mV)")
        self.tree.heading("fall", text="Chute %")
        self.tree.heading("temp", text="Temp °C")
        self.tree.heading("hr", text="BPM")
        self.tree.heading("ver", text="Version")

        for col in columns:
            self.tree.column(col, anchor="center", width=80)
        self.tree.pack(fill="both", expand=True, side="left")
        scrollbar = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscroll=scrollbar.set)
        scrollbar.pack(side="right", fill="y")

        # --- Panneau Architecture réseau (arbre des nœuds) ---
        arch_frame = tk.LabelFrame(
            self.root,
            text=" Architecture réseau (Tree Mesh) ",
            font=("Segoe UI", 10, "bold"),
            bg="#ffffff",
            padx=8,
            pady=8,
        )
        arch_frame.pack(fill="x", padx=10, pady=5)
        self.tree_arch = ttk.Treeview(arch_frame, columns=("layer",), show="tree headings", height=8)
        self.tree_arch.heading("#0", text="Nœud")
        self.tree_arch.heading("layer", text="Couche")
        self.tree_arch.column("#0", width=200)
        self.tree_arch.column("layer", width=60)
        scroll_arch = ttk.Scrollbar(arch_frame, orient="vertical", command=self.tree_arch.yview)
        self.tree_arch.pack(side="left", fill="both", expand=True)
        scroll_arch.pack(side="right", fill="y")
        self.tree_arch.configure(yscrollcommand=scroll_arch.set)

        # --- Panneau OTA ---
        ota_frame = tk.LabelFrame(
            self.root,
            text=" Contrôle OTA (fichier .bin → ROOT par port série) ",
            font=("Segoe UI", 10, "bold"),
            bg="#ffffff",
            padx=10,
            pady=10,
        )
        ota_frame.pack(fill="x", padx=10, pady=10)

        tk.Button(ota_frame, text="Fichier .bin", command=self.browse_file).grid(
            row=0, column=0, padx=5, pady=5
        )
        tk.Label(
            ota_frame,
            textvariable=self.selected_file,
            bg="#ffffff",
            width=35,
            anchor="w",
        ).grid(row=0, column=1, padx=5, pady=5)

        self.btn_ota_local = tk.Button(
            ota_frame,
            text="Mise à jour locale OTA",
            command=lambda: self.trigger_ota_local(),
            bg="#fbbc04",
        )
        self.btn_ota_local.grid(row=0, column=2, padx=10, pady=5)
        tk.Button(
            ota_frame,
            text="Lancer OTA Mesh",
            command=lambda: self.trigger_ota_mesh(),
            bg="#1a73e8",
            fg="white",
        ).grid(row=0, column=3, padx=10, pady=5)
        self.btn_test_serial = tk.Button(
            ota_frame,
            text="Test liaison (0xFF)",
            command=self.test_serial_link,
            bg="#9e9e9e",
            fg="white",
        )
        self.btn_test_serial.grid(row=0, column=4, padx=10, pady=5)

        tk.Label(
            ota_frame,
            text="OTA Série (0x01) = flash ROOT seul, LED violette. OTA Mesh (0x02) = ROOT reçoit puis diffuse (PULL), LED bleue ; nœuds : LED orange (téléchargement) puis 4× vert si OK. Local (0x01)=rapide, Mesh (0x02)=diffusion. LED violet/bleu. Mise à jour locale (0x01) = nœud seul, très rapide. OTA Mesh (0x02) = ROOT diffuse. LED violet/bleu ; l’état 4× vert = OK.",
            bg="#ffffff",
            fg="#666",
            font=("Segoe UI", 9),
        ).grid(row=1, column=0, columnspan=5, sticky="w", padx=5, pady=2)
        self.progress_ota = ttk.Progressbar(ota_frame, maximum=100, length=400, mode="determinate")
        self.progress_ota.grid(row=2, column=0, columnspan=5, padx=5, pady=5, sticky="ew")
        self.lbl_ota_progress = tk.Label(ota_frame, text="", bg="#ffffff", fg="#333", font=("Segoe UI", 9))
        self.lbl_ota_progress.grid(row=3, column=0, columnspan=5, padx=5, pady=2)

        # --- Zone logs : script (gauche) + série (droite) ---
        logs_container = tk.Frame(self.root, bg="#f0f2f5")
        logs_container.pack(fill="both", expand=True, padx=10, pady=5)

        # Barre outil logs : bouton Pause
        logs_toolbar = tk.Frame(logs_container, bg="#f0f2f5", height=28)
        logs_toolbar.pack(side="top", fill="x")
        self.btn_pause_logs = tk.Button(
            logs_toolbar,
            text="Pause",
            command=self.toggle_pause_logs,
            bg="#34a853",
            fg="white",
            font=("Segoe UI", 9),
            relief="flat",
            padx=12,
            pady=2,
        )
        self.btn_pause_logs.pack(side="right", padx=4, pady=2)
        tk.Label(logs_toolbar, text="Logs", bg="#f0f2f5", font=("Segoe UI", 9, "bold")).pack(side="left", padx=2)

        # Panneau gauche : logs du script (connexion, OTA, etc.)
        left_log_frame = tk.LabelFrame(
            logs_container,
            text=" Logs script ",
            font=("Segoe UI", 9, "bold"),
            bg="#f0f2f5",
            fg="#333",
        )
        left_log_frame.pack(side="left", fill="both", expand=True, padx=(0, 4))
        scroll_log = ttk.Scrollbar(left_log_frame)
        self.txt_logs = tk.Text(
            left_log_frame, height=10, bg="#202124", fg="#8ab4f8", font=("Consolas", 9),
            yscrollcommand=scroll_log.set,
        )
        scroll_log.config(command=self.txt_logs.yview)
        self.txt_logs.pack(side="left", fill="both", expand=True, padx=2, pady=2)
        scroll_log.pack(side="right", fill="y")

        # Panneau droit : logs série (ESP/firmware) avec filtre par tag
        right_log_frame = tk.LabelFrame(
            logs_container,
            text=" Log série (ESP / firmware) ",
            font=("Segoe UI", 9, "bold"),
            bg="#f0f2f5",
            fg="#333",
        )
        right_log_frame.pack(side="right", fill="both", expand=True, padx=(4, 0))
        # Barre filtre par tag
        tag_toolbar = tk.Frame(right_log_frame, bg="#f0f2f5", height=28)
        tag_toolbar.pack(side="top", fill="x", padx=2, pady=2)
        tk.Label(tag_toolbar, text="Tags à afficher :", bg="#f0f2f5", font=("Segoe UI", 8)).pack(side="left", padx=(0, 6))
        defaults = {"BOOT": True, "SERIE": True, "OTA": True, "MESH": False, "ROUTING": True, "TOPOLOGY": True, "MAIN": True, "TASK": True, "ERREUR": True, "SCRIPT": True, "AUTRE": True}
        for tag_id in self.LOG_SERIAL_TAG_IDS:
            var = tk.BooleanVar(value=defaults.get(tag_id, True))
            self.log_serial_tag_vars[tag_id] = var
            cb = tk.Checkbutton(
                tag_toolbar, text=tag_id, variable=var, bg="#f0f2f5",
                font=("Segoe UI", 8), activebackground="#f0f2f5",
                command=lambda: None,
            )
            cb.pack(side="left", padx=2)
        tk.Checkbutton(
            tag_toolbar, text="Tout", variable=self.log_serial_show_all_var, bg="#f0f2f5",
            font=("Segoe UI", 8, "bold"), activebackground="#f0f2f5",
        ).pack(side="left", padx=(8, 0))
        scroll_serial = ttk.Scrollbar(right_log_frame)
        self.txt_serial_logs = tk.Text(
            right_log_frame, height=10, bg="#1e1e2e", fg="#a6e3a1", font=("Consolas", 9),
            yscrollcommand=scroll_serial.set,
        )
        scroll_serial.config(command=self.txt_serial_logs.yview)
        self.txt_serial_logs.pack(side="left", fill="both", expand=True, padx=2, pady=2)
        scroll_serial.pack(side="right", fill="y")

    def toggle_pause_logs(self):
        """Met en pause ou reprend l'affichage des logs (script + série)."""
        self.logs_paused = not self.logs_paused
        if self.logs_paused:
            self.btn_pause_logs.config(text="Reprendre", bg="#ea4335")
        else:
            self.btn_pause_logs.config(text="Pause", bg="#34a853")

    def log(self, message):
        """Ajoute un message dans la zone « Logs script » avec tag [SCRIPT] pour identifier l’origine."""
        if self.logs_paused:
            return
        t = time.strftime("%H:%M:%S")
        self.txt_logs.insert(tk.END, f"[{t}] [SCRIPT] {message}\n")
        self.txt_logs.see(tk.END)

    def _serial_log_tag_from_line(self, line):
        """Retourne le tag de la ligne (ex. '[BOOT]', '[ROUTING]', '[OTA]') ou '[AUTRE]' si aucun tag reconnu."""
        line_raw = (line or "").strip()
        if line_raw.startswith("[TOPOLOGY]"):
            return "[TOPOLOGY]"
        if line_raw.startswith("["):
            end = line_raw.find("]", 1)
            if end > 0:
                tag = line_raw[: end + 1]
                tag_upper = tag.upper()
                for id_ in self.LOG_SERIAL_TAG_IDS:
                    if tag_upper == f"[{id_}]":
                        return f"[{id_}]"
        if " ROUTING:" in line_raw or line_raw.startswith("ROUTING:"):
            return "[ROUTING]"
        # Logs ESP-IDF du gestionnaire OTA (OTA_TREE) et lignes [SERIE] contenant "OTA"
        if " OTA_TREE:" in line_raw or (line_raw.startswith("[SERIE]") and "OTA" in line_raw.upper()):
            return "[OTA]"
        return "[AUTRE]"

    def _serial_log_should_show(self, line):
        """True si la ligne doit être affichée selon les cases à cocher des tags."""
        if self.log_serial_show_all_var.get():
            return True
        tag = self._serial_log_tag_from_line(line)
        tag_id = tag[1:-1].upper()  # BOOT, SERIE, ...
        var = self.log_serial_tag_vars.get(tag_id)
        if var is not None:
            return var.get()
        return self.log_serial_tag_vars.get("AUTRE", tk.BooleanVar(value=True)).get() if self.log_serial_tag_vars else True

    def append_serial_log(self, text):
        """Ajoute du texte dans la fenêtre des logs série (thread-safe), en respectant le filtre par tag."""
        if self.logs_paused:
            return
        def _append():
            if self.logs_paused:
                return
            if not self._serial_log_should_show(text):
                return
            self.txt_serial_logs.insert(tk.END, text)
            self.txt_serial_logs.see(tk.END)
        self.root.after(0, _append)

    def refresh_ports(self):
        self.combo_ports["values"] = [p.device for p in serial.tools.list_ports.comports()]
        if self.combo_ports["values"] and not self.combo_ports.get():
            self.combo_ports.set(self.combo_ports["values"][0])

    def toggle_serial(self):
        if self.serial_thread and self.serial_thread.running:
            self.serial_thread.running = False
            self.btn_serial.config(text="Connecter Série", bg="#e8f0fe")
            self.log("Déconnexion Série.")
        else:
            port = self.combo_ports.get()
            if not port:
                messagebox.showerror("Erreur", "Sélectionnez un port.")
                return
            self.serial_thread = SerialReader(
                port, BAUD_SERIAL, self.process_data, self.log, self.append_serial_log,
                topology_callback=self.process_topology,
                ota_mesh_ack_queue=self.ota_mesh_ack_queue,
            )
            self.serial_thread.start()
            self.btn_serial.config(text="Déconnecter", bg="#f28b82")

    def setup_mqtt(self):
        self.client = mqtt.Client()
        self.client.on_connect = lambda c, u, f, rc: self.on_mqtt_connect(rc)
        self.client.on_message = lambda c, u, m: self.process_data(m.payload.decode())
        try:
            self.client.connect_async(MQTT_BROKER, MQTT_PORT)
            self.client.loop_start()
        except Exception as e:
            self.log(f"MQTT non dispo: {e}")

    def on_mqtt_connect(self, rc):
        if rc == 0:
            self.lbl_mqtt.config(text="MQTT: Connecté", fg="green")
            self.client.subscribe(TOPIC_DATA)
        else:
            self.log(f"MQTT Erreur {rc}")

    def process_data(self, raw_json):
        """Traite une ligne JSON reçue (trame mesh convertie par le ROOT)."""
        try:
            data = json.loads(raw_json)
            node_id = data.get("nodeId", data.get("nodeShortId", "?"))
            node_key = str(node_id)
            parent_id = data.get("parentId")
            layer = data.get("layer", 0)
            if parent_id is not None and (parent_id == 0xFFFF or parent_id == 65535):
                parent_id = None
            self.topology[node_key] = {"parentId": parent_id, "layer": layer}

            temp = data.get("tempExt", 0)
            if isinstance(temp, (int, float)):
                temp_str = f"{temp:.1f}°C"
            else:
                temp_str = str(temp)
            parent_str = str(parent_id) if parent_id is not None else "—"
            info = [
                node_key,
                time.strftime("%H:%M:%S"),
                parent_str,
                str(layer),
                str(data.get("vBat", 0)),
                str(data.get("probFallLidar", 0)),
                temp_str,
                str(data.get("heartRate", "-")),
                str(data.get("fw_ver", "?")),
            ]
            self.root.after(0, self.update_tree, node_key, info)
            self.root.after(0, self.refresh_architecture)
        except json.JSONDecodeError:
            pass
        except Exception:
            pass

    def process_topology(self, raw_json):
        """Met à jour la topologie à partir d'une ligne [TOPOLOGY] du ROOT."""
        try:
            data = json.loads(raw_json)
            root_id = data.get("root")
            nodes = data.get("nodes", [])
            self.topology = {}
            if root_id is not None:
                self.topology[str(root_id)] = {"parentId": None, "layer": 0}
            for n in nodes:
                nid = n.get("id")
                if nid is not None:
                    pid = n.get("parentId")
                    if pid == 0xFFFF or pid == 65535:
                        pid = None
                    self.topology[str(nid)] = {"parentId": pid, "layer": n.get("layer", 0)}
            self.root.after(0, self.refresh_architecture)
        except json.JSONDecodeError:
            pass
        except Exception:
            pass

    def refresh_architecture(self):
        """Reconstruit l'arbre d'architecture à partir de self.topology."""
        for i in self.tree_arch.get_children(""):
            self.tree_arch.delete(i)
        if not self.topology:
            return

        def _norm_parent(pid):
            if pid is None or pid == 0xFFFF or pid == 65535:
                return None
            return str(pid)

        # Insérer par couche (layer 0 d'abord, puis 1, 2...) pour que le parent existe toujours
        by_layer = {}
        for nid, info in self.topology.items():
            layer = info.get("layer", 0)
            by_layer.setdefault(layer, []).append((nid, info))
        inserted = set()
        for layer in sorted(by_layer.keys()):
            for nid, info in by_layer[layer]:
                pid = _norm_parent(info.get("parentId"))
                parent_iid = "" if (pid is None or pid not in self.topology) else pid
                if parent_iid != "" and parent_iid not in inserted:
                    continue
                label = f"Nœud {nid}" + (" (ROOT)" if layer == 0 else "")
                self.tree_arch.insert(parent_iid, "end", iid=nid, text=label, values=(layer,))
                inserted.add(nid)

    def update_tree(self, node_id, info):
        if node_id in self.nodes:
            self.tree.item(self.nodes[node_id], values=info)
        else:
            self.nodes[node_id] = self.tree.insert("", "end", values=info)

    def browse_file(self):
        f = filedialog.askopenfilename(filetypes=[("Binaire", "*.bin")])
        if f:
            self.full_path = f
            self.selected_file.set(f.split("/")[-1].split("\\")[-1])

    def _ota_worker(self, mode):
        """Thread : envoi mode (1 byte) + en-tête 38 octets + blocs 200 octets. mode=OTA_MODE_SERIAL_ROOT ou OTA_MODE_MESH."""
        path = self.full_path
        ser_reader = self.serial_thread
        if not path or not os.path.isfile(path):
            self.root.after(0, lambda: self.log("[SERIE] OTA : aucun fichier .bin sélectionné."))
            return
        if not ser_reader or not ser_reader.ser or not ser_reader.ser.is_open:
            self.root.after(0, lambda: self.log("[SERIE] OTA : port série non connecté."))
            return
        name = "Mise à jour locale OTA" if mode == OTA_MODE_SERIAL_ROOT else "OTA Mesh"
        try:
            with open(path, "rb") as f:
                data = f.read()
            size = len(data)
            total_chunks = (size + OTA_CHUNK_SIZE - 1) // OTA_CHUNK_SIZE
            md5 = hashlib.md5(data).hexdigest()
            if len(md5) != 32:
                self.root.after(0, lambda: self.log("[SERIE] Erreur : MD5 invalide."))
                return
            header = struct.pack("<I", size) + struct.pack("<H", total_chunks) + md5.encode("ascii")
            if len(header) != OTA_ADV_HEADER_SIZE:
                self.root.after(0, lambda: self.log("[SERIE] Erreur : en-tête invalide (≠ 38 octets)."))
                return
            # Envoi octet de mode (1 byte) puis pause pour laisser le ROOT le lire et logger
            mode_hex = "0x01" if mode == OTA_MODE_SERIAL_ROOT else "0x02"
            self.root.after(0, lambda: self.log(f"[SERIE] Envoi 1 octet mode {mode_hex} ({name}) vers le port..."))
            self.root.after(0, lambda: self.progress_ota.config(value=0))
            self.root.after(0, lambda: self.lbl_ota_progress.config(text=""))
            ser_reader.send_raw(bytes([mode]))
            time.sleep(0.15 if mode == OTA_MODE_SERIAL_ROOT else 0.2)
            self.root.after(0, lambda: self.log(f"[SERIE] Envoi en-tête 38 octets : taille={size}, chunks={total_chunks}, MD5={md5[:8]}..."))
            ser_reader.send_raw(header)
            ack_queue = getattr(self, "ota_mesh_ack_queue", None)
            if mode == OTA_MODE_SERIAL_ROOT:
                time.sleep(0.2)
            else:
                time.sleep(0.3)
                while ack_queue and not ack_queue.empty():
                    try:
                        ack_queue.get_nowait()
                    except queue.Empty:
                        break
            if mode == OTA_MODE_SERIAL_ROOT:
                for i in range(total_chunks):
                    start = i * OTA_CHUNK_SIZE
                    chunk = data[start : start + OTA_CHUNK_SIZE]
                    if len(chunk) < OTA_CHUNK_SIZE:
                        chunk = chunk + b"\xff" * (OTA_CHUNK_SIZE - len(chunk))
                    ser_reader.send_raw(chunk)
                    time.sleep(0.004)
                    if (i + 1) % 50 == 0 or i == total_chunks - 1:
                        self.root.after(0, lambda n=i + 1, t=total_chunks: self.log(f"[SERIE] Chunk {n}/{t} envoyé."))
                self.root.after(0, lambda: self.progress_ota.config(value=100))
                self.root.after(0, lambda: self.log("[SERIE] Envoi terminé. Nœud vérifie puis redémarre (LED violette → 4× vert si OK)."))
            else:
                # OTA Mesh (0x02) : 1 envoi = 1 chunk ; attente CHUNK_PROPAGATED (index i) avant le suivant
                for i in range(total_chunks):
                    start = i * OTA_CHUNK_SIZE
                    chunk = data[start : start + OTA_CHUNK_SIZE]
                    if len(chunk) < OTA_CHUNK_SIZE:
                        chunk = chunk + b"\xff" * (OTA_CHUNK_SIZE - len(chunk))
                    if (i % 100) == 0 or i < 3:
                        self.root.after(0, lambda n=i + 1, t=total_chunks, ii=i: self.log(f"[SERIE] Envoi chunk {n}/{t}, attente CHUNK_PROPAGATED {ii}..."))
                    ser_reader.send_raw(chunk)
                    try:
                        if ack_queue is not None:
                            got = ack_queue.get(timeout=120)
                            if i == 0 and got == 0:
                                self.root.after(0, lambda: self.log("[SERIE] Premier CHUNK_PROPAGATED reçu (index 0), synchro OK."))
                            if got != i:
                                self.root.after(0, lambda g=got, ii=i: self.log(f"[SERIE] Erreur sync: reçu CHUNK_PROPAGATED {g}, attendu {ii}. Arrêt."))
                                break
                    except queue.Empty:
                        self.root.after(0, lambda: self.log("[SERIE] Timeout 120 s en attente de CHUNK_PROPAGATED. Arrêt."))
                        break
                    pct = int((i + 1) * 100 / total_chunks)
                    self.root.after(0, lambda p=pct, n=i + 1, t=total_chunks: (
                        self.progress_ota.config(value=p),
                        self.lbl_ota_progress.config(text=f"Chunk {n}/{t} propagé ({p} %)"),
                    ))
                self.root.after(0, lambda: self.progress_ota.config(value=100))
                self.root.after(0, lambda: self.lbl_ota_progress.config(text="Propagation terminée. En attente des REBOOT_ACK..."))
                self.root.after(0, lambda: self.log("[SERIE] OTA Mesh : tous les chunks propagés. Enfants redémarrent, ROOT reprend."))
        except Exception as e:
            self.root.after(0, lambda: self.log(f"[SERIE] Erreur : {e}"))

    def test_serial_link(self):
        """Envoie un octet 0xFF pour tester si le ROOT reçoit (doit afficher 'Octet mode reçu: 0xFF' + 'Mode inconnu')."""
        if not self.serial_thread or not self.serial_thread.ser or not self.serial_thread.ser.is_open:
            messagebox.showwarning("Non connecté", "Connectez d'abord le port série au nœud ROOT.")
            return
        self.log("[SERIE] Test liaison : envoi 1 octet 0xFF vers le ROOT...")
        try:
            self.serial_thread.send_raw(bytes([0xFF]))
            self.log("[SERIE] Octet 0xFF envoyé. Regardez le log série ROOT : vous devez voir '[SERIE] Octet mode reçu: 0xFF' et 'Mode inconnu'.")
        except Exception as e:
            self.log(f"[SERIE] Erreur test : {e}")

    def trigger_ota_local(self):
        """Mise à jour locale OTA : nœud connecté uniquement, flux très rapide (0x01). Tâches stoppées côté firmware."""
        if not self.serial_thread or not self.serial_thread.ser or not self.serial_thread.ser.is_open:
            messagebox.showwarning("Non connecté", "Connectez d'abord le port série au nœud.")
            return
        if not self.full_path or not os.path.isfile(self.full_path):
            messagebox.showwarning("Fichier manquant", "Sélectionnez un fichier .bin avant de lancer l'OTA.")
            return
        self.log("[SERIE] Mise à jour locale OTA (0x01) : envoi rapide → nœud stoppe les tâches, reçoit, vérifie, reboot.")
        t = threading.Thread(target=self._ota_worker, args=(OTA_MODE_SERIAL_ROOT,), daemon=True)
        t.start()

    def trigger_ota_mesh(self):
        """Mise à jour des nœuds par le mesh (ROOT reçoit par série et diffuse sur le mesh, mode 0x02)."""
        if not self.serial_thread or not self.serial_thread.ser or not self.serial_thread.ser.is_open:
            messagebox.showwarning("Non connecté", "Connectez d'abord le port série au nœud ROOT.")
            return
        if not self.full_path or not os.path.isfile(self.full_path):
            messagebox.showwarning("Fichier manquant", "Sélectionnez un fichier .bin avant de lancer l'OTA.")
            return
        self.log("[SERIE] Démarrage OTA Mesh : envoi 0x02 + 38 octets + chunks → ROOT diffuse sur le mesh.")
        t = threading.Thread(target=self._ota_worker, args=(OTA_MODE_MESH,), daemon=True)
        t.start()


if __name__ == "__main__":
    root = tk.Tk()
    app = LexacareApp(root)
    root.mainloop()
