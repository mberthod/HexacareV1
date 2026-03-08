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

BAUD_RATE = 115200

class MeshManagerV2:
    def __init__(self, root):
        self.root = root
        self.root.title("Hexacare V2 - Tree Mesh Manager (1000+ Nodes)")
        self.root.geometry("1100x600")
        
        self.nodes = {}
        self.serial_thread = None
        self.ser = None
        self.running = False
        
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

        self.btn_ota = tk.Button(top_frame, text="Lancer OTA Arbre (Tree)", command=self.trigger_ota, bg="#fbbc04")
        self.btn_ota.pack(side="right", padx=5)

        # Treeview (Now includes Layer and Parent ID for Mesh Tree monitoring)
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
        self.log_console = tk.Text(self.root, height=10, bg="#1e1e2e", fg="#a6e3a1")
        self.log_console.pack(fill="x", padx=10, pady=5)

    def log(self, msg):
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
                self.ser = serial.Serial(port, BAUD_RATE, timeout=1)
                self.running = True
                self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
                self.serial_thread.start()
                self.btn_connect.config(text="Déconnecter", bg="#f28b82")
                self.log(f"Connecté au ROOT sur {port}")
            except Exception as e:
                messagebox.showerror("Erreur", str(e))

    def read_serial(self):
        while self.running and self.ser.is_open:
            try:
                if self.ser.in_waiting:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line.startswith("{") and line.endswith("}"):
                        self.parse_json(line)
                    else:
                        self.root.after(0, self.log, line)
            except Exception as e:
                pass

    def parse_json(self, raw):
        try:
            data = json.loads(raw)
            # Adapté pour le nouveau Tree Protocol
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
        except Exception:
            pass

    def trigger_ota(self):
        if not self.running:
            messagebox.showwarning("Erreur", "Connectez-vous d'abord.")
            return
        file_path = filedialog.askopenfilename(filetypes=[("Binary", "*.bin")])
        if file_path:
            threading.Thread(target=self.send_ota, args=(file_path,), daemon=True).start()

    def send_ota(self, path):
        # Format identique à votre V1, mais le firmware V2 gérera la propagation en arbre
        try:
            with open(path, "rb") as f: data = f.read()
            size = len(data)
            chunks = (size + 199) // 200
            md5 = hashlib.md5(data).hexdigest()
            
            header = struct.pack("<I", size) + struct.pack("<H", chunks) + md5.encode("ascii")
            self.root.after(0, self.log, f"Lancement OTA (Arbre)... Taille: {size}b, MD5: {md5[:6]}")
            
            self.ser.write(bytes([0x02])) # 0x02 = Mode OTA Mesh
            time.sleep(0.5)
            self.ser.write(header)
            time.sleep(0.5)
            
            for i in range(chunks):
                chunk = data[i*200 : (i+1)*200]
                chunk = chunk + b'\xff' * (200 - len(chunk))
                self.ser.write(chunk)
                time.sleep(0.03) # Délai pour éviter le buffer overflow série
            
            self.root.after(0, self.log, "Transfert OTA terminé vers le ROOT. Propagation dans l'arbre en cours...")
        except Exception as e:
            self.root.after(0, self.log, f"Erreur OTA: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = MeshManagerV2(root)
    root.mainloop()