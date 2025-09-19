import socket
import optparse
import time
import sys
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
import customtkinter as ctk
from tkinter import messagebox
import json
import time
from datetime import datetime
from PIL import Image
import threading
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

# IP details for the TCP server
DEFAULT_IP = '192.168.18.10'  # IP address of the TCP server
DEFAULT_PORT = 50007          # Port of the TCP server
BUFFER_SIZE = 1024
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

# Configuración de tema personalizado negro/ámbar
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("dark-blue")

# Colores personalizados
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

#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class KUKADashboard(ctk.CTk):
    def __init__(self, server_ip, server_port):
        super().__init__()
        self.title("KUKA Robot Dashboard - Control y Monitoreo")
        self.geometry("1200x800")
        self.server_ip = server_ip
        self.server_port = server_port

   # Estado del robot (inicialmente desconectado)
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




#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////#####
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(1.0)  # Timeout opcional para recv
        self.create_dashboard()        
        try:
            self.sock.connect((self.server_ip, self.server_port))
            self.robot_status["connected"] = True
            self.add_log("[SISTEMA] Conectado al servidor", "system")       
        except Exception as e:
               self.robot_status["connected"] = False
               self.add_log(f"[SISTEMA] No se pudo conectar: {e}", "error")
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////#####
        self.running = True
        self.status_thread_active = False  # Nueva variable para controlar el hilo de STATUS
        self.last_message_time = 0  # Timestamp del último mensaje recibido
        self.connection_timeout = 5  # Segundos sin respuesta para considerar desconexión
        
     
        
        self.configure(fg_color=COLOR_BG)

        
        # Iniciar hilo de comunicación
        threading.Thread(target=self.listen_server, daemon=True).start()
        # Iniciar actualización de datos
        threading.Thread(target=self.update_data_loop, daemon=True).start()
        # Iniciar monitor de conexión
        threading.Thread(target=self.connection_monitor, daemon=True).start()
        
        self.protocol("WM_DELETE_WINDOW", self.on_close)
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    def create_dashboard(self):
        # Frame principal con scroll
        self.main_frame = ctk.CTkScrollableFrame(self, fg_color=COLOR_BG)
        self.main_frame.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Header con título y estado de conexión
        self.create_header()
        
        # Grid principal modificado (2 columnas x 2 filas)
        self.create_grid_layout()
    
    def create_header(self):
        self.header_frame = ctk.CTkFrame(self.main_frame, fg_color=COLOR_CARD, height=80)
        self.header_frame.pack(fill="x", pady=(0, 15))
        self.header_frame.pack_propagate(False)
        
        # Título
        self.title_label = ctk.CTkLabel(
            self.header_frame,
            text="KUKA Robot Control Dashboard",
            font=("Segoe UI", 24, "bold"),
            text_color=COLOR_FG
        )
        self.title_label.pack(side="left", padx=20, pady=20)
        
        # Estado de conexión
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
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
    
    def create_control_panel(self, row, col):
        card = ctk.CTkFrame(self.grid_frame, fg_color=COLOR_CARD)
        card.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")
        
        title = ctk.CTkLabel(card, text="Control de Salidas", font=("Segoe UI", 16, "bold"), text_color=COLOR_FG)
        title.pack(pady=(10, 15))
        
        # Control general - Encender/Apagar todas las salidas
        general_frame = ctk.CTkFrame(card, fg_color="transparent")
        general_frame.pack(fill="x", padx=10, pady=(0, 15))
        
        ctk.CTkLabel(general_frame, text="Control General:", font=("Segoe UI", 14, "bold"), text_color=COLOR_TEXTW).pack()
        
        general_buttons = ctk.CTkFrame(general_frame, fg_color="transparent")
        general_buttons.pack(fill="x", pady=5)
        
        self.all_on_btn = ctk.CTkButton(
            general_buttons, text="ENCENDER TODO", 
            fg_color=COLOR_SUCCESS, hover_color="#00CC66",
            font=("Segoe UI", 12, "bold"), 
            command=self.turn_all_on
        )
        self.all_on_btn.pack(side="left", expand=True, fill="x", padx=(0, 5))
        
        self.all_off_btn = ctk.CTkButton(
            general_buttons, text="APAGAR TODO", 
            fg_color=COLOR_ERROR, hover_color="#CC3333",
            font=("Segoe UI", 12, "bold"), 
            command=self.turn_all_off
        )
        self.all_off_btn.pack(side="right", expand=True, fill="x", padx=(5, 0))
        
        # Separador
        separator = ctk.CTkFrame(card, fg_color=COLOR_FG, height=2)
        separator.pack(fill="x", padx=10, pady=10)
        
        # Control individual de las 4 salidas
        ctk.CTkLabel(card, text="Control Individual:", font=("Segoe UI", 14, "bold"), text_color=COLOR_TEXTW).pack(pady=(5, 10))
        
        # Crear controles individuales para cada salida
        self.output_switches = []
        self.output_labels = []
        
        for i in range(4):
            output_frame = ctk.CTkFrame(card, fg_color="transparent")
            output_frame.pack(fill="x", padx=10, pady=5)
            
            # Label de la salida
            output_label = ctk.CTkLabel(
                output_frame, 
                text=f"Salida {i+1}: OFF", 
                font=("Segoe UI", 12, "bold"), 
                text_color=COLOR_ERROR
            )
            output_label.pack(side="left")
            
            # Switch ON/OFF
            switch = ctk.CTkSwitch(
                output_frame, 
                text="", 
                command=lambda idx=i: self.toggle_output(idx),
                button_color=COLOR_SUCCESS,
                progress_color=COLOR_SUCCESS
            )
            switch.pack(side="right")
            
            self.output_switches.append(switch)
            self.output_labels.append(output_label)
#/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
####################################################################################################
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
#########################################################################################################################

    # Nuevo método para monitorear la conexión
    def connection_monitor(self):
        """Monitorea la conexión y marca como desconectado si no hay respuesta"""
        while self.running:
            try:
                current_time = time.time()
                
                # Si está conectado y han pasado más de connection_timeout segundos sin respuesta
                if (self.robot_status["connected"] and 
                    self.last_message_time > 0 and 
                    current_time - self.last_message_time > self.connection_timeout):
                    
                    # Marcar como desconectado
                    self.robot_status["connected"] = False
                    self.status_thread_active = False
                    self.update_connection_status()
                    self.add_log("[SISTEMA] Conexión perdida - Sin respuesta del servidor", "error")
                    
                    # Resetear estados de salidas por seguridad
                    for i in range(4):
                        self.robot_status["outputs"][i] = False
                        # Actualizar switches sin triggear comandos
                        old_command = self.output_switches[i]._command
                        self.output_switches[i].configure(command=None)
                        self.output_switches[i].deselect()
                        self.output_switches[i].configure(command=old_command)
                    
                    self.update_output_display()
                
                time.sleep(1)  # Verificar cada segundo
                
            except Exception as e:
                if self.running:
                    self.add_log(f"[ERROR] Error en monitor de conexión: {e}", "error")
                time.sleep(1)
######################################################## Conecion ##########################################333    
    # Nuevo método para enviar STATUS cada segundo
    def status_sender_loop(self):
        """Envía STATUS cada segundo mientras esté conectado"""
        while self.status_thread_active and self.running:
            if self.robot_status["connected"]:
                try:
########################################################Se realizo cambio                 
                # En TCP usamos send en lugar de sendto
                # Agregamos un salto de línea si el servidor espera líneas separadas
                 self.sock.send("STATUS\n".encode('utf-8'))
                # No agregamos al log para evitar spam, solo mensajes importantes
                except Exception as e:
                    self.add_log(f"[ERROR] Error enviando STATUS: {e}", "error")
                    break
            time.sleep(1)

#########################################################################################################################3
    # Método para procesar respuesta CSV del STATUS
    def process_csv_status(self, csv_data):
        """Procesa la respuesta CSV del servidor y actualiza las salidas"""
        try:
            # Limpiar datos CSV
            csv_data = csv_data.strip()
            
            # Dividir por comas
            values = csv_data.split(',')
            
            if len(values) >= 4:
                # Actualizar el estado de las salidas basado en el CSV
                for i in range(4):
                    try:
                        output_state = bool(int(values[i]))
                        self.robot_status["outputs"][i] = output_state
                        
                        # Actualizar switches sin triggear el comando
                        if output_state != self.output_switches[i].get():
                            # Temporalmente desactivar el callback
                            old_command = self.output_switches[i]._command
                            self.output_switches[i].configure(command=None)
                            
                            if output_state:
                                self.output_switches[i].select()
                            else:
                                self.output_switches[i].deselect()
                            
                            # Restaurar el callback
                            self.output_switches[i].configure(command=old_command)
                            
                    except (ValueError, IndexError) as e:
                        self.add_log(f"[ERROR] Error procesando salida {i+1}: {e}", "error")
                
                # Actualizar display visual
                self.update_output_display()
                
                # Actualizar estado general
                self.robot_status["all_outputs"] = all(self.robot_status["outputs"])
                
                # Log solo cuando hay cambios significativos (opcional)
                # self.add_log(f"[STATUS] Salidas actualizadas: {csv_data}", "received")
                
            else:
                self.add_log(f"[ERROR] CSV inválido recibido: {csv_data}", "error")
                
        except Exception as e:
            self.add_log(f"[ERROR] Error procesando CSV '{csv_data}': {e}", "error")
############################################################################################################################

    # Funciones de control de salidas
    def turn_all_on(self):
        """Enciende todas las salidas"""
        for i, switch in enumerate(self.output_switches):
            if not switch.get():
                switch.select()
                self.robot_status["outputs"][i] = True
        
        self.robot_status["all_outputs"] = True
        self.send_command("ALL_ON")
        self.update_output_display()
    
    def turn_all_off(self):
        """Apaga todas las salidas"""
        for i, switch in enumerate(self.output_switches):
            if switch.get():
                switch.deselect()
                self.robot_status["outputs"][i] = False
        
        self.robot_status["all_outputs"] = False
        self.send_command("ALL_OFF")
        self.update_output_display()
    
    def toggle_output(self, output_index):
        """Controla una salida individual"""
        switch_state = self.output_switches[output_index].get()
        self.robot_status["outputs"][output_index] = switch_state
        
        command = f"{output_index + 1}_{'ON' if switch_state else 'OFF'}"
        self.send_command(command)
        self.update_output_display()
    
    def update_output_display(self):
        """Actualiza los labels de las salidas y los switches"""
        for i, (switch, label) in enumerate(zip(self.output_switches, self.output_labels)):
            state = self.robot_status["outputs"][i]
            
            # Actualizar el texto y color del label
            if state:
                label.configure(text=f"Salida {i+1}: ON", text_color=COLOR_SUCCESS)
            else:
                label.configure(text=f"Salida {i+1}: OFF", text_color=COLOR_ERROR)
            
            # Actualizar el estado del switch si es necesario
            if switch.get() != state:
                # Desactivar temporalmente el comando para evitar envío automático
                old_command = switch._command
                switch.configure(command=None)
                
                if state:
                    switch.select()
                else:
                    switch.deselect()
                # Restaurar el comando
                switch.configure(command=old_command)
#####################################Cambio de Codigo a TCP#########################################################################################
    def send_command(self, command):
        try:
         self.sock.send(command.encode('utf-8'))  # TCP no usa sendto
         self.robot_status["last_command"] = command
         self.add_log(f"[ENVIADO] {command}", "sent")
        except Exception as e:
            self.add_log(f"[ERROR] No se pudo enviar '{command}': {e}", "error")
    
    def send_custom_command(self):
        command = self.command_entry.get().strip()
        if command:
            self.send_command(command)
            self.command_entry.delete(0, "end")
############################################Cambia a TCP##################################################################################################

    def listen_server(self):
        while self.running:
            try:
                data = self.sock.recv(BUFFER_SIZE)  # TCP
                msg = data.decode('utf-8')
                
                # Actualizar timestamp del último mensaje recibido
                self.last_message_time = time.time()
                
                self.process_server_message(msg)
                self.add_log(f"[RECIBIDO] {msg}", "received")
                    
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    self.add_log(f"[ERROR] Error de conexión: {e}", "error")
                break
###############################################################################################################

    def process_server_message(self, msg):
        """Procesa mensajes del servidor y actualiza el estado del robot"""
        try:
            # Verificar mensajes específicos de conexión
            if msg.strip() == "Accesado":
                self.robot_status["connected"] = True
                self.last_message_time = time.time()  # Actualizar timestamp
                self.update_connection_status()
                self.add_log("[SISTEMA] Acceso concedido - Conectado al servidor", "system")
                
                # INICIAR el hilo de envío de STATUS cada segundo
                if not self.status_thread_active:
                    self.status_thread_active = True
                    threading.Thread(target=self.status_sender_loop, daemon=True).start()
                    self.add_log("[SISTEMA] Iniciado envío automático de STATUS", "system")
                
                return
                
            elif msg.strip() == "Acceso_DENEGADO":
                self.robot_status["connected"] = False
                self.status_thread_active = False  # Detener envío de STATUS
                self.last_message_time = 0  # Resetear timestamp
                self.update_connection_status()
                self.add_log("[SISTEMA] Acceso denegado - Desconectado del servidor", "error")
                return
            
            # Verificar si es una respuesta CSV (formato: "0,0,0,0" o similar)
            if ',' in msg and len(msg.split(',')) >= 4:
                # Es probable que sea una respuesta de STATUS en formato CSV
                try:
                    # Verificar que todos los valores sean números (0 o 1)
                    values = msg.strip().split(',')
                    if all(val.strip() in ['0', '1'] for val in values[:4]):
                        self.process_csv_status(msg)
                        return
                except:
                    pass  # Si falla, continuar con el procesamiento normal
            
            # Si el mensaje es JSON, actualizamos el estado
            if msg.startswith("{") and msg.endswith("}"):
                data = json.loads(msg)
                self.robot_status.update(data)
                
        except json.JSONDecodeError:
            # Mensaje de texto simple, solo lo mostramos en el log
            pass
########################################################################################################33
    def update_data_loop(self):
        """Actualiza la interfaz cada segundo"""
        while self.running:
            self.update_display()
            time.sleep(1)
    
    def update_display(self):
        """Actualiza todos los elementos visuales"""
        try:
            # Actualizar información del sistema
            self.time_label.configure(text=datetime.now().strftime("%H:%M:%S"))
            
            # Simular incremento de uptime solo si está conectado
            if self.robot_status["connected"]:
                self.robot_status["uptime"] += 1
            
            hours = self.robot_status["uptime"] // 3600
            minutes = (self.robot_status["uptime"] % 3600) // 60
            seconds = self.robot_status["uptime"] % 60
            self.uptime_label.configure(text=f"{hours:02d}:{minutes:02d}:{seconds:02d}")
            
            # Actualizar display de salidas
            self.update_output_display()
            
        except Exception as e:
            print(f"Error actualizando display: {e}")
    
    def update_connection_status(self):
        """Actualiza el indicador de conexión"""
        if self.robot_status["connected"]:
            self.connection_status.configure(text="CONECTADO", text_color=COLOR_SUCCESS)
        else:
            self.connection_status.configure(text="DESCONECTADO", text_color=COLOR_ERROR)
            # Reiniciar uptime cuando se desconecta
            self.robot_status["uptime"] = 0
            # Resetear timestamp de último mensaje
            self.last_message_time = 0
    
    def add_log(self, message, msg_type="normal"):
        """Añade mensaje al log con timestamp"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        formatted_msg = f"[{timestamp}] {message}\n"
        
        self.log_area.configure(state="normal")
        
        # Configurar color según tipo
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
##################################################################################################

    def clear_log(self):
        """Limpia el área de log"""
        self.log_area.configure(state="normal")
        self.log_area.delete("1.0", "end")
        self.log_area.configure(state="disabled")
        self.add_log("Log limpiado", "system")
    
    def on_close(self):
        self.running = False
        self.status_thread_active = False  # Detener el hilo de STATUS
        self.sock.close()
        self.destroy()

class ConfigDialog(ctk.CTkToplevel):
    def __init__(self, parent, default_ip, default_port):
        super().__init__(parent)
        self.title("Configuración del Cliente TCP")
        self.geometry("700x300")
        self.resizable(False, False)
        self.default_ip = default_ip
        self.default_port = default_port
        self.result = None
        
        self.configure(fg_color=COLOR_BG)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        # Frame principal dividido en 2 columnas
        self.main_frame = ctk.CTkFrame(self, fg_color=COLOR_BG)
        self.main_frame.pack(fill="both", expand=True, padx=0, pady=0)

        # Frame izquierdo para la imagen y el título
        self.left_frame = ctk.CTkFrame(self.main_frame, width=250, fg_color="#000000")
        self.left_frame.pack(side="left", fill="both", expand=False)
        self.left_frame.pack_propagate(False)

        # Título en la parte superior izquierda
        self.left_title = ctk.CTkLabel(
            self.left_frame,
            text="Dashboard KUKA \n Control y Monitoreo",
            text_color=COLOR_TEXT,
            font=("Segoe UI", 20, "bold"),
            anchor="center"
        )
        self.left_title.pack(pady=(20, 10))

        # Cargar y mostrar la imagen
        try:
            # Cargar la imagen Log.png
            self.logo_image = ctk.CTkImage(
                light_image=Image.open("Log.png"),
                dark_image=Image.open("Log.png"),
                size=(120, 120)  # Ajusta el tamaño según necesites
            )
            
            # Label para mostrar la imagen
            self.logo_label = ctk.CTkLabel(
                self.left_frame,
                image=self.logo_image,
                text=""  # Sin texto, solo imagen
            )
            self.logo_label.pack(pady=(10, 20))
            
        except Exception as e:
            # Si no se puede cargar la imagen, mostrar un mensaje alternativo
            self.error_label = ctk.CTkLabel(
                self.left_frame,
                text="[Logo no encontrado]",
                text_color=COLOR_ERROR,
                font=("Segoe UI", 12)
            )
            self.error_label.pack(pady=(10, 20))
            print(f"No se pudo cargar la imagen Log.png: {e}")

        # Frame derecho para los widgets
        self.right_frame = ctk.CTkFrame(self.main_frame, fg_color=COLOR_FG)
        self.right_frame.pack(side="right", fill="both", expand=True, padx=1, pady=1)

        # Título
        self.title_label = ctk.CTkLabel(
            self.right_frame,
            text="Para establecer conexión \n agregar estos datos:",
            text_color=COLOR_TEXTW,
            font=("Segoe UI", 16, "bold", "italic")
        )
        self.title_label.pack(pady=(15, 5))

        # Widgets de configuración
        self.ip_label = ctk.CTkLabel(self.right_frame, text="IP del Servidor:", text_color=COLOR_TEXTW, font=("Segoe UI", 16, "bold", "underline"))
        self.ip_label.pack(pady=(10, 0))
        
        self.ip_entry = ctk.CTkEntry(self.right_frame, 
                                   fg_color=COLOR_ENTRY, 
                                   border_color=COLOR_FG,
                                   text_color=COLOR_TEXT)
        self.ip_entry.insert(0, self.default_ip)
        self.ip_entry.pack(pady=5)
        
        self.port_label = ctk.CTkLabel(self.right_frame, text="Puerto del Servidor:", text_color=COLOR_TEXTW, font=("Segoe UI", 16, "bold", "underline"))
        self.port_label.pack(pady=(10, 0))
        
        self.port_entry = ctk.CTkEntry(self.right_frame, 
                                     fg_color=COLOR_ENTRY, 
                                     border_color=COLOR_FG,
                                     text_color=COLOR_TEXT)
        self.port_entry.insert(0, str(self.default_port))
        self.port_entry.pack(pady=5)
        
        self.connect_btn = ctk.CTkButton(
            self.right_frame, 
            text="Conectar al Dashboard", 
            fg_color=COLOR_BTN,
            hover_color=COLOR_HOVER,
            text_color=COLOR_TEXTW,
            command=self.validate,
            font=("Segoe UI", 13, "bold")
        )
        self.connect_btn.pack(pady=10)
        
        self.grab_set()
#########################################################################################

    def validate(self):
        ip = self.ip_entry.get().strip()
        port_str = self.port_entry.get().strip()
        try:
            socket.inet_aton(ip)
            port = int(port_str)
            if not (0 < port < 65536):
                raise ValueError
            self.result = (ip, port)
            self.destroy()
        except Exception:
            messagebox.showerror("Error", "Por favor ingrese una dirección IP y puerto válidos (1-65535).")
    
    def on_close(self):
        self.result = None
        self.destroy()

if __name__ == '__main__':
    config_window = ctk.CTk()
    config_window.withdraw()
    config_dialog = ConfigDialog(config_window, DEFAULT_IP, DEFAULT_PORT)
    config_window.wait_window(config_dialog)
    
    if config_dialog.result is None:
        exit(0)
    
    server_ip, server_port = config_dialog.result
    app = KUKADashboard(server_ip, server_port)
    app.mainloop()
