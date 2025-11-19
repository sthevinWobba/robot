import pygame
import socket
import time
import sys

# =======================================================================
# === CONFIGURACIÓN ===
# =======================================================================

# --- Configuración de Red UDP ---
ESP32_IP = "192.168.1.100"  # ¡Cámbiala a la IP real de tu ESP32!
ESP32_PORT = 2390           # Debe coincidir con el puerto en el código del ESP32

# --- Configuración del Joystick Logitech F310 ---
JOYSTICK_DEADZONE = 0.1     # Ignorar movimientos pequeños cerca del centro
MAX_JOY_VALUE = 100         # Rango máximo de valores enviados al ESP32 (±100)
UDP_SEND_RATE = 0.02        # Frecuencia de envío: 50 Hz (20ms)

# --- Mapeo de Ejes para Logitech F310 (modo DirectInput) ---
# Eje 0: Stick Izquierdo Horizontal (X) - Giro
# Eje 1: Stick Izquierdo Vertical (Y) - Adelante/Atrás
AXIS_FORWARD = 1  # Eje Y del stick izquierdo
AXIS_TURN = 0     # Eje X del stick izquierdo

# --- Mapeo de Botones para Logitech F310 ---
# Botón 0: A (verde)
# Botón 1: B (rojo)
BUTTON_DISCO_1 = 0  # Botón A
BUTTON_DISCO_2 = 1  # Botón B

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
print("🎮 CONTROL REMOTO PARA ROBOT ESP32")
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
print(f"\n🕹️  Controles:")
print(f"   - Stick Izquierdo: Movimiento (Adelante/Atrás/Giro)")
print(f"   - Botón A (verde): Disco 1")
print(f"   - Botón B (rojo): Disco 2")
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

print("\n🚀 Iniciando control remoto...\n")

try:
    while True:
        # Procesar eventos de pygame (necesario para actualizar el estado del joystick)
        pygame.event.pump()
        
        # --- Lectura de Ejes (Para el control del vehículo) ---
        
        # Leer Eje Y (Adelante/Atrás)
        # NOTA: Invertimos el signo porque en la mayoría de joysticks,
        # empujar hacia arriba da -1.0, pero queremos que sea positivo
        try:
            raw_y = -joystick.get_axis(AXIS_FORWARD)
        except (pygame.error, IndexError):
            raw_y = 0.0
            
        # Leer Eje X (Giro Izquierda/Derecha)
        try:
            raw_x = joystick.get_axis(AXIS_TURN)
        except (pygame.error, IndexError):
            raw_x = 0.0

        # Aplicar deadzone y mapear a rango -100 a 100
        joy_y = map_joystick_to_range(raw_y, JOYSTICK_DEADZONE, MAX_JOY_VALUE)
        joy_x = map_joystick_to_range(raw_x, JOYSTICK_DEADZONE, MAX_JOY_VALUE)

        # --- Lectura de Botones (Para los discos giratorios) ---
        
        try:
            btn_disco_1 = joystick.get_button(BUTTON_DISCO_1)
        except (pygame.error, IndexError):
            btn_disco_1 = 0
        
        try:
            btn_disco_2 = joystick.get_button(BUTTON_DISCO_2)
        except (pygame.error, IndexError):
            btn_disco_2 = 0

        # --- Crear el Paquete de Datos ---
        # Formato: "Y:joyY,X:joyX,B1:btn1,B2:btn2"
        data_packet = f"Y:{joy_y},X:{joy_x},B1:{btn_disco_1},B2:{btn_disco_2}"

        # --- Enviar el Paquete UDP ---
        try:
            sock.sendto(data_packet.encode('utf-8'), (ESP32_IP, ESP32_PORT))
            
            # Mostrar el paquete enviado (con indicadores visuales)
            # Crear indicadores de dirección
            forward_indicator = "↑" if joy_y > 10 else ("↓" if joy_y < -10 else "·")
            turn_indicator = "→" if joy_x > 10 else ("←" if joy_x < -10 else "·")
            disco1_indicator = "🟢" if btn_disco_1 else "⚫"
            disco2_indicator = "🔴" if btn_disco_2 else "⚫"
            
            print(f"📤 {forward_indicator}{turn_indicator} Y:{joy_y:4d} X:{joy_x:4d} | D1:{disco1_indicator} D2:{disco2_indicator}   ", end='\r')
            
        except socket.error as e:
            print(f"\n⚠️  ERROR de red: {e}                    ")
            time.sleep(0.5)  # Esperar antes de reintentar

        # Controlar la frecuencia de envío
        time.sleep(UDP_SEND_RATE)

except KeyboardInterrupt:
    print("\n\n⏹️  Deteniendo el controlador...")
    
    # Enviar comando de parada antes de cerrar
    try:
        stop_packet = "Y:0,X:0,B1:0,B2:0"
        sock.sendto(stop_packet.encode('utf-8'), (ESP32_IP, ESP32_PORT))
        print("✅ Comando de parada enviado al ESP32")
    except socket.error:
        print("⚠️  No se pudo enviar comando de parada")
    
finally:
    # Asegurarse de cerrar todo limpiamente
    sock.close()
    pygame.quit()
    print("👋 Programa terminado correctamente.\n")