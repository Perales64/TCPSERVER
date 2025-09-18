import socket
import threading
import customtkinter as ctk
from tkinter import messagebox
import json
import time
from datetime import datetime
from PIL import Image


BUFFER_SIZE = 1024
DEFAULT_IP = '192.168.31.125'
DEFAULT_PORT = 57345

# Configuración de tema personalizado
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("dark-blue")

COLOR_BG = "#0D0D0D"
COLOR_FG = "#E68C05"
COLOR_BTN = "#333333"
COLOR_ENTRY = "#0D0D0D"
COLOR_TEXT = "#FF9900"
COLOR_TEXTW = "#F3DDBB"
COLOR_HOVER = "#F2B705"
COLOR_SUCCESS = "#00FF88"
COLOR_WARNING = "#FFB000"
COLOR_ERROR = "#FF4444"
COLOR_CARD = "#1A1A1A"


class KUKADashboard(ctk.CTk):
    def clear_log(self):
        if self.log_area is not None:
            self.log_area.configure(state="normal")
            self.log_area.delete("1.0", "end")
            self.log_area.configure(state="disabled")
    def send_custom_command(self):
        cmd = self.command_entry.get().strip()
        if cmd:
            self.send_command(cmd)
            self.command_entry.delete(0, 'end')
        else:
            self.add_log("[SISTEMA] El comando está vacío", "system")
    def __init__(self, server_ip, server_port):
        super().__init__()
        self.title("KUKA Robot Dashboard - Control y Monitoreo")
        self.geometry("1200x800")
        self.server_ip = server_ip
        self.server_port = server_port
        self.sock = None
        self.running = True
        self.status_thread_active = False
        self.last_message_time = 0
        self.connection_timeout = 5

        # Inicializar log_area como None para evitar errores de acceso antes de su creación
        self.log_area = None

        # Estado inicial del robot
        self.robot_status = {
            "connected": False,  # Estado inicial: desconectado
            "outputs": [False, False, False, False],  # Estado de las 4 salidas
            "temperature": 25,
            "speed": 0,
            "mode": "MANUAL",
            "error_count": 0,
            "last_command": "NONE",
            "uptime": 0,
            "all_outputs": False  # Estado general de todas las salidas
        }

        self.configure(fg_color=COLOR_BG)
        self.create_dashboard()

        # Intento de conexión TCP
        self.connect_to_server()
        
        # Iniciar hilo de comunicación
        threading.Thread(target=self.listen_server, daemon=True).start()
        # Hilos de actualización y monitor de conexión
        threading.Thread(target=self.update_data_loop, daemon=True).start()
        threading.Thread(target=self.connection_monitor, daemon=True).start()

        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def create_dashboard(self):
        self.main_frame = ctk.CTkScrollableFrame(self, fg_color=COLOR_BG)
        self.main_frame.pack(fill="both", expand=True, padx=10, pady=10)

        # Header con título y estado de conexión
        self.create_header()

        # Grid principal con control de salidas y log
        self.create_grid_layout()

    def create_header(self):
        self.header_frame = ctk.CTkFrame(self.main_frame, fg_color=COLOR_CARD, height=80)
        self.header_frame.pack(fill="x", pady=(0, 15))
        self.header_frame.pack_propagate(False)

        self.title_label = ctk.CTkLabel(
            self.header_frame,
            text="KUKA Robot Control Dashboard",
            font=("Segoe UI", 24, "bold"),
            text_color=COLOR_FG
        )
        self.title_label.pack(side="left", padx=20, pady=20)

        self.connection_frame = ctk.CTkFrame(self.header_frame, fg_color="transparent")
        self.connection_frame.pack(side="right", padx=20, pady=20)

        self.connection_status = ctk.CTkLabel(
            self.connection_frame,
            text="DESCONECTADO",
            font=("Segoe UI", 14, "bold"),
            text_color=COLOR_ERROR
        )
        self.connection_status.pack()

        self.server_info = ctk.CTkLabel(
            self.connection_frame,
            text=f"Servidor: {self.server_ip}:{self.server_port}",
            font=("Segoe UI", 10),
            text_color=COLOR_TEXT
        )
        self.server_info.pack()

    def create_grid_layout(self):
        # Frame contenedor para el grid
        self.grid_frame = ctk.CTkFrame(self.main_frame, fg_color="transparent")
        self.grid_frame.pack(fill="both", expand=True)
        
        # Configurar grid 2x2
        self.grid_frame.grid_columnconfigure((0, 1), weight=1)
        self.grid_frame.grid_rowconfigure((0, 1, 2), weight=1)
        
        # Fila 1
        self.create_control_panel(0, 0)  # Panel de control de salidas
        self.create_system_info(0, 1)    # Información del sistema
        
        # Fila 2
        self.create_quick_commands(1, 0, columnspan=2) # Comandos rápidos expandido
        
        # Fila 3
        self.create_log_panel(2, 0, columnspan=2)  # Log

    def create_control_panel(self, row, col, columnspan=1):
        card = ctk.CTkFrame(self.grid_frame, fg_color=COLOR_CARD)
        card.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")

        title = ctk.CTkLabel(card, text="Control de Salidas", font=("Segoe UI", 16, "bold"), text_color=COLOR_FG)
        title.pack(pady=(10, 15))

        self.output_switches = []
        self.output_labels = []

        for i in range(4):
            output_frame = ctk.CTkFrame(card, fg_color="transparent")
            output_frame.pack(fill="x", padx=10, pady=5)

            output_label = ctk.CTkLabel(output_frame, text=f"Salida {i+1}: OFF", font=("Segoe UI", 12, "bold"), text_color=COLOR_ERROR)
            output_label.pack(side="left")

            switch = ctk.CTkSwitch(output_frame, text="", command=lambda idx=i: self.toggle_output(idx),
                                   button_color=COLOR_SUCCESS, progress_color=COLOR_SUCCESS)
            switch.pack(side="right")

            self.output_switches.append(switch)
            self.output_labels.append(output_label)

    def create_log_panel(self, row, col, columnspan=1):
        card = ctk.CTkFrame(self.grid_frame, fg_color=COLOR_CARD)
        card.grid(row=row, column=col, columnspan=columnspan, padx=5, pady=5, sticky="nsew")
        
        title = ctk.CTkLabel(card, text="Log de Comunicación", font=("Segoe UI", 16, "bold"), text_color=COLOR_FG)
        title.pack(pady=(10, 5))
        
        # Área de log
        self.log_area = ctk.CTkTextbox(
            card, height=150,
            fg_color=COLOR_ENTRY, border_color=COLOR_FG,
            text_color=COLOR_TEXT, font=("Consolas", 10)
        )
        self.log_area.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.log_area.configure(state="disabled")
        
        # Botón para limpiar log
        ctk.CTkButton(
            card, text="Limpiar Log", height=30,
            fg_color=COLOR_BTN, hover_color=COLOR_HOVER,
            command=self.clear_log
        ).pack(pady=(0, 10))

    def create_quick_commands(self, row, col, columnspan=1):
        card = ctk.CTkFrame(self.grid_frame, fg_color=COLOR_CARD)
        card.grid(row=row, column=col, columnspan=columnspan, padx=5, pady=5, sticky="nsew")
        
        title = ctk.CTkLabel(card, text="Comandos Rápidos", font=("Segoe UI", 16, "bold"), text_color=COLOR_FG)
        title.pack(pady=(10, 5))
        
        # Frame contenedor para organizar en dos columnas
        commands_container = ctk.CTkFrame(card, fg_color="transparent")
        commands_container.pack(fill="both", expand=True, padx=10, pady=5)
        
        # Columna izquierda
        left_column = ctk.CTkFrame(commands_container, fg_color="transparent")
        left_column.pack(side="left", fill="both", expand=True, padx=(0, 5))
        
        # Campo de entrada personalizado
        self.command_entry = ctk.CTkEntry(
            left_column, placeholder_text="Comando personalizado...",
            fg_color=COLOR_ENTRY, border_color=COLOR_FG,
            text_color=COLOR_TEXT, font=("Segoe UI", 11)
        )
        self.command_entry.pack(fill="x", pady=(0, 5))
        self.command_entry.bind("<Return>", lambda e: self.send_custom_command())
        
        self.send_custom_btn = ctk.CTkButton(
            left_column, text="Enviar Comando",
            fg_color=COLOR_BTN, hover_color=COLOR_HOVER,
            font=("Segoe UI", 11, "bold"),
            command=self.send_custom_command
        )
        self.send_custom_btn.pack(fill="x", pady=(0, 10))
        
        # Columna derecha - Botones predefinidos
        right_column = ctk.CTkFrame(commands_container, fg_color="transparent")
        right_column.pack(side="right", fill="both", expand=True, padx=(5, 0))
        
        commands = ["STATUS", "Inicio"]
        for cmd in commands:
            btn = ctk.CTkButton(
                right_column, text=cmd, height=35,
                fg_color=COLOR_BTN, hover_color=COLOR_HOVER,
                font=("Segoe UI", 10), command=lambda c=cmd: self.send_command(c)
            )
            btn.pack(fill="x", pady=2)
    
    def create_system_info(self, row, col):
        card = ctk.CTkFrame(self.grid_frame, fg_color=COLOR_CARD)
        card.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")
        
        title = ctk.CTkLabel(card, text="Información del Sistema", font=("Segoe UI", 16, "bold"), text_color=COLOR_FG)
        title.pack(pady=(10, 5))
        
        info_frame = ctk.CTkFrame(card, fg_color="transparent")
        info_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        # Uptime
        uptime_frame = ctk.CTkFrame(info_frame, fg_color="transparent")
        uptime_frame.pack(fill="x", pady=5)
        ctk.CTkLabel(uptime_frame, text="Tiempo activo:", font=("Segoe UI", 12), text_color=COLOR_TEXTW).pack(side="left")
        self.uptime_label = ctk.CTkLabel(uptime_frame, text="00:00:00", font=("Consolas", 12), text_color=COLOR_TEXT)
        self.uptime_label.pack(side="right")
        
        # Timestamp
        time_frame = ctk.CTkFrame(info_frame, fg_color="transparent")
        time_frame.pack(fill="x", pady=5)
        ctk.CTkLabel(time_frame, text="Hora actual:", font=("Segoe UI", 12), text_color=COLOR_TEXTW).pack(side="left")
        self.time_label = ctk.CTkLabel(time_frame, text="", font=("Consolas", 12), text_color=COLOR_TEXT)
        self.time_label.pack(side="right")
    
    # -------------------- Funciones de conexión TCP --------------------
    def connect_to_server(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(0.5)
            self.sock.connect((self.server_ip, self.server_port))
            self.robot_status["connected"] = True
            self.update_connection_status()
            self.add_log(f"[SISTEMA] Conectado a {self.server_ip}:{self.server_port}", "system")

            # Iniciar hilo de escucha
            threading.Thread(target=self.listen_server, daemon=True).start()

        except Exception as e:
            self.robot_status["connected"] = False
            self.update_connection_status()
            messagebox.showwarning("Advertencia", f"No se pudo conectar al servidor: {e}")

    def listen_server(self):
        while self.running and self.robot_status["connected"]:
            try:
                data = self.sock.recv(BUFFER_SIZE)
                if not data:
                    break
                msg = data.decode("utf-8").strip()
                self.add_log(f"[RECIBIDO] {msg}", "received")

            except Exception as e:
                self.add_log(f"[ERROR] Error de conexión: {e}", "error")
                break

    # -------------------- Control de salidas --------------------
    def toggle_output(self, idx):
        switch_state = self.output_switches[idx].get()
        self.robot_status["outputs"][idx] = switch_state
        cmd = f"{idx+1}_{'ON' if switch_state else 'OFF'}"
        self.send_command(cmd)
        self.update_output_display()

    def update_output_display(self):
        for i, (switch, label) in enumerate(zip(self.output_switches, self.output_labels)):
            state = self.robot_status["outputs"][i]
            label.configure(text=f"Salida {i+1}: {'ON' if state else 'OFF'}", text_color=COLOR_SUCCESS if state else COLOR_ERROR)

    def send_command(self, cmd):
        try:
            if self.robot_status["connected"]:
                self.sock.send(cmd.encode("utf-8"))
                self.add_log(f"[ENVIADO] {cmd}", "sent")
        except Exception as e:
            self.add_log(f"[ERROR] No se pudo enviar '{cmd}': {e}", "error")

    # -------------------- UI y logs --------------------
    def add_log(self, message, msg_type="normal"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        formatted_msg = f"[{timestamp}] {message}\n"
        if self.log_area is not None:
            self.log_area.configure(state="normal")
            if msg_type == "sent":
                color = COLOR_SUCCESS
            elif msg_type == "received":
                color = COLOR_FG
            elif msg_type == "error":
                color = COLOR_ERROR
            elif msg_type == "system":
                color = COLOR_WARNING
            else:
                color = COLOR_TEXT

            self.log_area.tag_config(msg_type, foreground=color)
            self.log_area.insert("end", formatted_msg, msg_type)
            self.log_area.see("end")
            self.log_area.configure(state="disabled")
        else:
            print(formatted_msg)

    def update_data_loop(self):
        while self.running:
            self.update_display()
            threading.Event().wait(1)

    def update_display(self):
        self.update_output_display()
        self.update_connection_status()

    def update_connection_status(self):
        if self.robot_status["connected"]:
            self.connection_status.configure(text="CONECTADO", text_color=COLOR_SUCCESS)
        else:
            self.connection_status.configure(text="DESCONECTADO", text_color=COLOR_ERROR)

    def connection_monitor(self):
        while self.running:
            if self.robot_status["connected"] and self.last_message_time > 0:
                import time
                if time.time() - self.last_message_time > self.connection_timeout:
                    self.robot_status["connected"] = False
                    self.update_connection_status()
                    self.add_log("[SISTEMA] Conexión perdida", "error")
            import time
            time.sleep(1)

    def on_close(self):
        self.running = False
        if self.sock:
            self.sock.close()
        self.destroy()


if __name__ == '__main__':
    app = KUKADashboard(DEFAULT_IP, DEFAULT_PORT)
    try:
        app.mainloop()
    except Exception as e:
        print(f"[ERROR] {e}")
        if hasattr(app, 'destroy'):
            app.destroy()
