import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import serial
import serial.tools.list_ports
import threading
import json
import time
import struct
import hashlib
import os
import re

BAUD_RATE = 115200

class MeshManagerV2Advanced:
    def __init__(self, root):
        self.root = root
        self.root.title("Hexacare V2 - Advanced Tree Mesh Manager")
        self.root.geometry("1200x700")
        
        self.nodes = {}
        self.serial_thread = None
        self.ser = None
        self.running = False
        
        # Synchronisation OTA
        self.ota_in_progress = False
        self.ota_ack_event = threading.Event()
        
        self.setup_ui()

    def setup_ui(self):
        # Top Bar
        top_frame = tk.Frame(self.root, pady=10, padx=10)
        top_frame.pack(fill="x")
        
        tk.Label(top_frame, text="Port Série:").pack(side="left")
        self.cb_ports = ttk.Combobox(top_frame, values=[p.device for p in serial.tools.list_ports.comports()])
        self.cb_ports.pack(side="left", padx=5)
        if self.cb_ports['values']: self.cb_ports.current(0)
        
        self.btn_connect = tk.Button(top_frame, text="Connecter", command=self.toggle_connection, bg="#e8f0fe")
        self.btn_connect.pack(side="left", padx=5)

        self.btn_ota = tk.Button(top_frame, text="Lancer OTA Arbre (Sécurisé)", command=self.trigger_ota, bg="#fbbc04")
        self.btn_ota.pack(side="right", padx=5)

        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(top_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(side="right", fill="x", expand=True, padx=20)

        # Treeview (Monitoring des noeuds)
        cols = ("node", "layer", "parent", "battery", "hr", "temp", "status")
        self.tree = ttk.Treeview(self.root, columns=cols, show="headings")
        self.tree.heading("node", text="Node ID")
        self.tree.heading("layer", text="Couche (Layer)")
        self.tree.heading("parent", text="Parent ID")
        self.tree.heading("battery", text="Batterie (mV)")
        self.tree.heading("hr", text="Rythme Cardiaque")
        self.tree.heading("temp", text="Temp °C")
        self.tree.heading("status", text="Dernière Vue")
        
        for c in cols: self.tree.column(c, anchor="center", width=120)
        self.tree.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Log Console
        self.log_console = tk.Text(self.root, height=15, bg="#1e1e2e", fg="#a6e3a1", font=("Consolas", 10))
        self.log_console.pack(fill="x", padx=10, pady=5)

    def log(self, msg, color="#a6e3a1"):
        self.log_console.insert(tk.END, f"[{time.strftime('%H:%M:%S')}] {msg}\n")
        self.log_console.see(tk.END)

    def toggle_connection(self):
        if self.running:
            self.running = False
            self.btn_connect.config(text="Connecter", bg="#e8f0fe")
            if self.ser: self.ser.close()
            self.log("Déconnecté.")
        else:
            port = self.cb_ports.get()
            try:
                self.ser = serial.Serial(port, BAUD_RATE, timeout=0.1)
                self.running = True
                self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
                self.serial_thread.start()
                self.btn_connect.config(text="Déconnecter", bg="#f28b82")
                self.log(f"Connecté au ROOT sur {port}")
            except Exception as e:
                messagebox.showerror("Erreur", str(e))

    def read_serial(self):
        buffer = ""
        # Regex pour trouver des objets JSON valides même noyés dans du texte cassé
        json_pattern = re.compile(r'\{[^{}]*"nodeId"[^{}]*\}')

        while self.running and self.ser.is_open:
            try:
                if self.ser.in_waiting:
                    raw_data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    buffer += raw_data
                    
                    # Traitement ligne par ligne
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        
                        if not line: continue

                        # Gestion de l'accusé de réception OTA (ACK)
                        if "OTA_CHUNK_OK" in line:
                            self.ota_ack_event.set()
                        
                        # Recherche de JSON valides dans la ligne
                        match = json_pattern.search(line)
                        if match:
                            self.parse_json(match.group(0))
                            # On logge la ligne sans le JSON pour éviter de polluer l'écran
                            clean_line = line.replace(match.group(0), "").strip()
                            if clean_line:
                                self.root.after(0, self.log, clean_line)
                        else:
                            # Log standard
                            self.root.after(0, self.log, line)
            except Exception as e:
                pass

    def parse_json(self, raw):
        try:
            data = json.loads(raw)
            node_id = str(data.get("nodeId", "?"))
            layer = str(data.get("layer", "?"))
            parent = str(data.get("parentId", "ROOT" if layer == "1" else "?"))
            bat = str(data.get("vBat", "?"))
            hr = str(data.get("heartRate", "?"))
            temp = f"{data.get('tempExt', 0):.1f}"
            now = time.strftime("%H:%M:%S")
            
            vals = (node_id, layer, parent, bat, hr, temp, now)
            
            def update():
                if node_id in self.nodes:
                    self.tree.item(self.nodes[node_id], values=vals)
                else:
                    self.nodes[node_id] = self.tree.insert("", "end", values=vals)
            self.root.after(0, update)
        except json.JSONDecodeError:
            pass # Ignorer les faux positifs de la regex

    def trigger_ota(self):
        if not self.running:
            messagebox.showwarning("Erreur", "Connectez-vous d'abord.")
            return
        if self.ota_in_progress:
            messagebox.showwarning("Erreur", "OTA déjà en cours.")
            return

        file_path = filedialog.askopenfilename(filetypes=[("Binary", "*.bin")])
        if file_path:
            threading.Thread(target=self.send_ota_secure, args=(file_path,), daemon=True).start()

    def send_ota_secure(self, path):
        self.ota_in_progress = True
        try:
            with open(path, "rb") as f: data = f.read()
            size = len(data)
            chunks = (size + 199) // 200
            md5 = hashlib.md5(data).hexdigest()
            
            header = struct.pack("<I", size) + struct.pack("<H", chunks) + md5.encode("ascii")
            self.root.after(0, self.log, f"--- DÉBUT OTA SÉCURISÉ --- Taille: {size}b, Chunks: {chunks}")
            
            # 1. Envoi de la commande de passage en mode OTA (pour couper les capteurs)
            self.ser.write(bytes([0x02])) 
            time.sleep(0.5)
            
            # 2. Envoi du Header
            self.ser.write(header)
            time.sleep(0.5)
            
            # 3. Envoi des chunks avec attente d'ACK
            for i in range(chunks):
                chunk = data[i*200 : (i+1)*200]
                chunk = chunk + b'\xff' * (200 - len(chunk)) # Padding
                
                self.ota_ack_event.clear() # Reset l'événement
                self.ser.write(chunk)
                
                # Attente de la réponse "OTA_CHUNK_OK" de l'ESP32 (Timeout de 2 secondes)
                ack_received = self.ota_ack_event.wait(2.0)
                
                if not ack_received:
                    self.root.after(0, self.log, f" ERREUR CRITIQUE: Timeout au chunk {i}/{chunks}. Annulation OTA.")
                    self.ota_in_progress = False
                    return
                
                # Mise à jour UI tous les 10 chunks
                if i % 10 == 0:
                    pct = (i / chunks) * 100
                    self.root.after(0, self.progress_var.set, pct)
            
            self.root.after(0, self.progress_var.set, 100)
            self.root.after(0, self.log, "--- OTA UART TERMINÉ AVEC SUCCÈS. Propagation Mesh en cours... ---")
        
        except Exception as e:
            self.root.after(0, self.log, f"Erreur fatale OTA: {e}")
        finally:
            self.ota_in_progress = False

if __name__ == "__main__":
    root = tk.Tk()
    app = MeshManagerV2Advanced(root)
    root.mainloop()