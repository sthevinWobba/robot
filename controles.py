import pygame
import socket
import time
import sys

# =======================================================================
# === CONFIGURACIÓN ===
# =======================================================================

# --- Configuración de Red UDP ---
ESP32_IP = "192.168.0.11"  # IP estática del ESP32

ESP32_PORT = 2390           # Debe coincidir con el puerto en el código del ESP32

# --- Configuración del Joystick Logitech F310 ---
JOYSTICK_DEADZONE = 0.1     # Ignorar movimientos pequeños cerca del centro
MAX_JOY_VALUE = 100         # Rango máximo de valores enviados al ESP32 (±100)
UDP_SEND_RATE = 0.02        # Frecuencia de envío: 50 Hz (20ms)

# --- Mapeo de Ejes para Logitech F310 - TANK DRIVE ---
# MODO TANK DRIVE: Cada stick controla un motor independientemente
AXIS_LEFT_MOTOR = 1   # Stick Izquierdo Vertical (Y) → Motor Izquierdo
AXIS_RIGHT_MOTOR = 3  # Stick Derecho Vertical (Y) → Motor Derecho

# --- Mapeo de Botones para Logitech F310 ---
# Botón 0: A (verde)
# Botón 1: B (rojo)
BUTTON_DISCO_1 = 0  # Botón A
BUTTON_DISCO_2 = 1  # Botón B

# --- Estado de Toggle para Botones ---
disco1_state = False  # Estado actual del disco 1 (False=OFF, True=ON)
disco2_state = False  # Estado actual del disco 2 (False=OFF, True=ON)
last_btn1_state = False  # Último estado del botón 1
last_btn2_state = False  # Último estado del botón 2

# =======================================================================
# === FUNCIONES AUXILIARES ===
# =======================================================================

def clamp(value, min_value, max_value):
    """Limita un valor entre min y max"""
    return max(min_value, min(max_value, value))

def apply_deadzone(value, deadzone):
    """Aplica zona muerta al joystick"""
    if abs(value) < deadzone:
        return 0.0
    return value

def map_joystick_to_range(raw_value, deadzone, max_output):
    """
    Convierte valor del joystick (-1.0 a 1.0) a rango de salida
    con zona muerta aplicada
    """
    # Aplicar deadzone
    value = apply_deadzone(raw_value, deadzone)
    
    # Mapear a rango de salida
    output = int(value * max_output)
    
    # Asegurar que está dentro del rango
    return clamp(output, -max_output, max_output)

# =======================================================================
# === INICIALIZACIÓN ===
# =======================================================================

print("=" * 60)
print("🎮 CONTROL REMOTO PARA ROBOT ESP32 - TANK DRIVE MODE")
print("=" * 60)

# Inicializa pygame
pygame.init()
pygame.joystick.init()

# Verificar Joystick
if pygame.joystick.get_count() == 0:
    print("❌ ERROR: No se detectó ningún Joystick.")
    print("   Conecta el control Logitech F310 y reinicia el programa.")
    pygame.quit()
    sys.exit(1)

# Selecciona el primer joystick detectado
joystick = pygame.joystick.Joystick(0)
joystick.init()

print(f"✅ Joystick detectado: {joystick.get_name()}")
print(f"   - Ejes disponibles: {joystick.get_numaxes()}")
print(f"   - Botones disponibles: {joystick.get_numbuttons()}")
print(f"\n📡 Configuración de red:")
print(f"   - IP del ESP32: {ESP32_IP}")
print(f"   - Puerto UDP: {ESP32_PORT}")
print(f"   - Frecuencia de envío: {1/UDP_SEND_RATE:.0f} Hz")
print(f"\n🕹️  Controles TANK DRIVE:")
print(f"   - Stick Izquierdo (Y): Motor Izquierdo")
print(f"   - Stick Derecho (Y): Motor Derecho")
print(f"   - Botón A (verde): Toggle Disco 1 (ON/OFF)")
print(f"   - Botón B (rojo): Toggle Disco 2 (ON/OFF)")
print(f"\n💡 Tip: Mueve ambos sticks hacia adelante para avanzar recto")
print(f"        Presiona A o B una vez para encender, otra vez para apagar")
print(f"\n⚠️  Presiona Ctrl+C para detener")
print("=" * 60)

# Inicializa el socket UDP
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Configurar timeout para operaciones de socket
    sock.settimeout(1.0)
except socket.error as e:
    print(f"❌ ERROR al crear socket UDP: {e}")
    pygame.quit()
    sys.exit(1)

# =======================================================================
# === BUCLE PRINCIPAL DE CONTROL ===
# =======================================================================

print("\n🚀 Iniciando control remoto en modo TANK DRIVE...\n")

try:
    while True:
        # Procesar eventos de pygame (necesario para actualizar el estado del joystick)
        pygame.event.pump()
        
        # --- TANK DRIVE: Lectura de Ambos Sticks Verticales ---
        
        # Leer Stick Izquierdo Y → Motor Izquierdo
        # NOTA: Invertimos el signo porque empujar hacia arriba da -1.0
        try:
            raw_left = -joystick.get_axis(AXIS_LEFT_MOTOR)
        except (pygame.error, IndexError):
            raw_left = 0.0
            
        # Leer Stick Derecho Y → Motor Derecho
        try:
            raw_right = -joystick.get_axis(AXIS_RIGHT_MOTOR)
        except (pygame.error, IndexError):
            raw_right = 0.0

        # Aplicar deadzone y mapear a rango -100 a 100
        motor_left = map_joystick_to_range(raw_left, JOYSTICK_DEADZONE, MAX_JOY_VALUE)
        motor_right = map_joystick_to_range(raw_right, JOYSTICK_DEADZONE, MAX_JOY_VALUE)

        # --- Lectura de Botones con TOGGLE (ON/OFF) ---
        # Detectar flanco de subida (botón recién presionado) y alternar estado
        
        try:
            current_btn1 = joystick.get_button(BUTTON_DISCO_1)
        except (pygame.error, IndexError):
            current_btn1 = False
        
        try:
            current_btn2 = joystick.get_button(BUTTON_DISCO_2)
        except (pygame.error, IndexError):
            current_btn2 = False

        # Detectar flanco de subida (botón recién presionado) y alternar estado
        if current_btn1 and not last_btn1_state:
            disco1_state = not disco1_state  # Toggle
        if current_btn2 and not last_btn2_state:
            disco2_state = not disco2_state  # Toggle
        
        # Actualizar estados anteriores
        last_btn1_state = current_btn1
        last_btn2_state = current_btn2
        
        # Convertir booleano a entero para enviar
        btn_disco_1 = 1 if disco1_state else 0
        btn_disco_2 = 1 if disco2_state else 0

        # --- Crear el Paquete de Datos ---
        # Formato TANK DRIVE: "L:motorLeft,R:motorRight,B1:btn1,B2:btn2"
        data_packet = f"L:{motor_left},R:{motor_right},B1:{btn_disco_1},B2:{btn_disco_2}"

        # --- Enviar el Paquete UDP ---
        try:
            sock.sendto(data_packet.encode('utf-8'), (ESP32_IP, ESP32_PORT))
            
            # Mostrar el paquete enviado con indicadores visuales
            # Crear indicadores de dirección para cada motor
            left_indicator = "↑" if motor_left > 10 else ("↓" if motor_left < -10 else "·")
            right_indicator = "↑" if motor_right > 10 else ("↓" if motor_right < -10 else "·")
            disco1_indicator = "🟢 ON " if btn_disco_1 else "⚫ OFF"
            disco2_indicator = "🔴 ON " if btn_disco_2 else "⚫ OFF"
            
            # Determinar tipo de movimiento
            if abs(motor_left - motor_right) < 10:
                if motor_left > 10:
                    movement = "⬆️ ADELANTE"
                elif motor_left < -10:
                    movement = "⬇️ ATRÁS"
                else:
                    movement = "⏸️ DETENIDO"
            elif motor_left > 10 and motor_right < -10:
                movement = "↪️ GIRO IZQ (en lugar)"
            elif motor_left < -10 and motor_right > 10:
                movement = "↩️ GIRO DER (en lugar)"
            elif motor_left > motor_right:
                movement = "↗️ GIRO DERECHA"
            else:
                movement = "↖️ GIRO IZQUIERDA"
            
            print(f"📤 {left_indicator} L:{motor_left:4d} | {right_indicator} R:{motor_right:4d} | {movement:22s} | D1:{disco1_indicator} D2:{disco2_indicator}   ", end='\r')
            
        except socket.error as e:
            print(f"\n⚠️  ERROR de red: {e}                    ")
            time.sleep(0.5)  # Esperar antes de reintentar

        # Controlar la frecuencia de envío
        time.sleep(UDP_SEND_RATE)

except KeyboardInterrupt:
    print("\n\n⏹️  Deteniendo el controlador...")
    
    # Enviar comando de parada antes de cerrar
    try:
        stop_packet = "L:0,R:0,B1:0,B2:0"
        sock.sendto(stop_packet.encode('utf-8'), (ESP32_IP, ESP32_PORT))
        print("✅ Comando de parada enviado al ESP32")
    except socket.error:
        print("⚠️  No se pudo enviar comando de parada")
    
finally:
    # Asegurarse de cerrar todo limpiamente
    sock.close()
    pygame.quit()
    print("👋 Programa terminado correctamente.\n")