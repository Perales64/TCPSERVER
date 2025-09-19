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
DEFAULT_PORT = 599

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
    def __init__(self, server_ip, server_port):
        super().__init__()
        self.title("KUKA Robot Dashboard - Control y Monitoreo")
        self.geometry("1200x800")
        self.server_ip = server_ip
        self.server_port = server_port
        
        # Inicialización de variables de control de conexión
        self.sock = None
        self.sock_lock = threading.Lock()  # Mutex para proteger el socket
        self.running = True
        self.connection_active = False
        self.listener_thread = None
        self.reconnect_thread = None
        
        self.last_message_time = 0
        self.connection_timeout = 10  # Aumentado el timeout
        self.start_time = time.time()
        
        # Inicializar log_area como None para evitar errores de acceso antes de su creación
        self.log_area = None

        # Estado inicial del robot
        self.robot_status = {
            "connected": False,
            "outputs": [False, False, False, False],
            "temperature": 25,
            "speed": 0,
            "mode": "MANUAL",
            "error_count": 0,
            "last_command": "NONE",
            "uptime": 0,
            "all_outputs": False
        }

        self.configure(fg_color=COLOR_BG)
        self.create_dashboard()
        
        # Hilos de actualización y monitor de conexión
        threading.Thread(target=self.update_data_loop, daemon=True).start()
        threading.Thread(target=self.connection_monitor, daemon=True).start()

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        
        # Intentar conexión inicial
        self.start_connection()

    def safe_socket_close(self):
        """Cerrar el socket de forma segura"""
        with self.sock_lock:
            if self.sock:
                try:
                    self.sock.shutdown(socket.SHUT_RDWR)
                except:
                    pass
                try:
                    self.sock.close()
                except:
                    pass
                self.sock = None
                self.connection_active = False

    def start_connection(self):
        """Iniciar conexión en un hilo separado"""
        if self.reconnect_thread and self.reconnect_thread.is_alive():
            return  # Ya hay un intento de conexión en curso
            
        self.reconnect_thread = threading.Thread(target=self.connect_to_server, daemon=True)
        self.reconnect_thread.start()

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
        self.create_control_panel(0, 0)
        self.create_system_info(0, 1)
        
        # Fila 2
        self.create_quick_commands(1, 0, columnspan=2)
        
        # Fila 3
        self.create_log_panel(2, 0, columnspan=2)

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
        """Conectar al servidor de forma segura"""
        try:
            # Cerrar conexión anterior si existe
            self.safe_socket_close()
            
            self.add_log(f"[SISTEMA] Intentando conectar a {self.server_ip}:{self.server_port}...", "system")
            
            # Crear nuevo socket
            new_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            new_socket.settimeout(10.0)  # Timeout para conexión
            
            # Intentar conectar
            new_socket.connect((self.server_ip, self.server_port))
            
            # Si llegamos aquí, la conexión fue exitosa
            with self.sock_lock:
                self.sock = new_socket
                self.connection_active = True
                self.sock.settimeout(1.0)  # Timeout para operaciones de lectura
            
            self.robot_status["connected"] = True
            self.last_message_time = time.time()
            self.update_connection_status()
            self.add_log(f"[SISTEMA] Conectado exitosamente a {self.server_ip}:{self.server_port}", "system")

            # Iniciar hilo de escucha
            self.listener_thread = threading.Thread(target=self.listen_server, daemon=True)
            self.listener_thread.start()

        except Exception as e:
            self.robot_status["connected"] = False
            self.connection_active = False
            self.update_connection_status()
            self.add_log(f"[SISTEMA] Error de conexión: {e}", "error")

    def listen_server(self):
        """Escuchar mensajes del servidor"""
        while self.running and self.connection_active:
            try:
                with self.sock_lock:
                    current_socket = self.sock
                
                if current_socket is None:
                    break
                    
                data = current_socket.recv(BUFFER_SIZE)
                if not data:
                    self.add_log("[SISTEMA] El servidor cerró la conexión", "error")
                    break
                    
                msg = data.decode("utf-8").strip()
                self.last_message_time = time.time()
                self.add_log(f"[RECIBIDO] {msg}", "received")

            except socket.timeout:
                # Timeout normal, continuar
                continue
            except socket.error as e:
                if self.connection_active:  # Solo mostrar error si la conexión debería estar activa
                    self.add_log(f"[ERROR] Error en socket: {e}", "error")
                break
            except Exception as e:
                if self.connection_active:
                    self.add_log(f"[ERROR] Error inesperado: {e}", "error")
                break
        
        # Limpiar al salir del bucle
        self.connection_active = False
        self.robot_status["connected"] = False

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
        """Enviar comando al servidor de forma segura"""
        try:
            with self.sock_lock:
                current_socket = self.sock
                is_connected = self.connection_active
            
            if is_connected and current_socket:
                try:
                    current_socket.send(cmd.encode("utf-8"))
                    self.add_log(f"[ENVIADO] {cmd}", "sent")
                    self.robot_status["last_command"] = cmd
                except socket.error as e:
                    self.add_log(f"[ERROR] Error enviando comando '{cmd}': {e}", "error")
                    self.connection_active = False
                    self.robot_status["connected"] = False
            else:
                self.add_log(f"[ERROR] No conectado - no se puede enviar '{cmd}'", "error")
        except Exception as e:
            self.add_log(f"[ERROR] Error inesperado enviando '{cmd}': {e}", "error")

    # -------------------- UI y logs --------------------
    def add_log(self, message, msg_type="normal"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        formatted_msg = f"[{timestamp}] {message}\n"
        
        if self.log_area is not None:
            try:
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
            except Exception as e:
                print(f"Error actualizando log: {e}")
        else:
            print(formatted_msg)

    def update_data_loop(self):
        while self.running:
            try:
                self.update_display()
            except Exception as e:
                print(f"Error en update_data_loop: {e}")
            time.sleep(1)

    def update_display(self):
        try:
            self.update_output_display()
            self.update_connection_status()
            self.update_time_display()
        except Exception as e:
            print(f"Error en update_display: {e}")

    def update_time_display(self):
        try:
            # Actualizar hora actual
            current_time = datetime.now().strftime("%H:%M:%S")
            self.time_label.configure(text=current_time)
            
            # Actualizar uptime
            uptime_seconds = int(time.time() - self.start_time)
            hours = uptime_seconds // 3600
            minutes = (uptime_seconds % 3600) // 60
            seconds = uptime_seconds % 60
            uptime_str = f"{hours:02d}:{minutes:02d}:{seconds:02d}"
            self.uptime_label.configure(text=uptime_str)
        except Exception as e:
            print(f"Error actualizando tiempo: {e}")

    def update_connection_status(self):
        try:
            if self.robot_status["connected"]:
                self.connection_status.configure(text="CONECTADO", text_color=COLOR_SUCCESS)
            else:
                self.connection_status.configure(text="DESCONECTADO", text_color=COLOR_ERROR)
        except Exception as e:
            print(f"Error actualizando estado de conexión: {e}")

    def connection_monitor(self):
        """Monitor de conexión con reintentos automáticos"""
        while self.running:
            try:
                # Verificar timeout de mensajes
                if (self.robot_status["connected"] and 
                    self.last_message_time > 0 and 
                    time.time() - self.last_message_time > self.connection_timeout):
                    
                    self.add_log("[SISTEMA] Timeout de conexión detectado", "error")
                    self.connection_active = False
                    self.robot_status["connected"] = False
                    self.safe_socket_close()
                
                # Intentar reconexión si no está conectado
                if not self.robot_status["connected"] and not self.connection_active:
                    if (not self.reconnect_thread or 
                        not self.reconnect_thread.is_alive()):
                        self.add_log("[SISTEMA] Intentando reconexión...", "system")
                        self.start_connection()
                        time.sleep(5)  # Esperar antes del siguiente intento
                        
            except Exception as e:
                print(f"Error en connection_monitor: {e}")
            
            time.sleep(2)  # Verificar cada 2 segundos

    def on_close(self):
        """Limpiar recursos al cerrar la aplicación"""
        self.running = False
        self.connection_active = False
        
        # Cerrar socket de forma segura
        self.safe_socket_close()
        
        # Esperar un momento para que los hilos terminen
        time.sleep(0.5)
        
        self.destroy()


if __name__ == '__main__':
    app = KUKADashboard(DEFAULT_IP, DEFAULT_PORT)
    try:
        app.mainloop()
    except Exception as e:
        print(f"[ERROR] {e}")
        if hasattr(app, 'destroy'):
            app.destroy()