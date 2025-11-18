# ----------------------------------------------------------------------
# SCRIPT CONTROLADOR DE PC PARA ROBOT DE BATALLA
# Función: Lee el Gamepad Logitech F310, normaliza los datos y los 
#          envía al ESP32 vía UDP.
# Requiere: Librerías pygame y socket.
# ----------------------------------------------------------------------
import pygame
import socket
import struct
import time
import sys

# --- CONFIGURACIÓN DE RED ---
# ¡IMPORTANTE! Reemplaza '192.168.1.100' con la IP real que obtiene tu ESP32.
TARGET_IP = "192.168.1.100" 
TARGET_PORT = 4200
SEND_INTERVAL = 0.02  # 20ms (Frecuencia de actualización de 50 Hz para baja latencia)

# --- CONFIGURACIÓN DE GAMEPAD (Logitech F310 en modo D o X) ---
# Estos índices asumen el MODO DIRECTINPUT (D) del F310.
AXIS_LEFT_Y = 1   # Joystick Izquierdo, Eje Y (Arriba/Abajo)
AXIS_LEFT_X = 0   # Joystick Izquierdo, Eje X (Izquierda/Derecha)
AXIS_R_TRIGGER = 5 # Gatillo Derecho (R2, usado para el arma)
BUTTON_KILL_SWITCH = 1 # Botón B (Rojo), usado como Kill Switch

# Inicialización de Pygame
pygame.init()
pygame.joystick.init()

# ----------------------------------------------------------------------
# LÓGICA DE CONTROL
# ----------------------------------------------------------------------

def map_axis_to_100(value):
    """
    Mapea el valor del eje de Pygame (-1.0 a 1.0) al rango del ESP32 (-100 a 100).
    """
    # Multiplica por 100 y redondea al entero más cercano
    return int(round(value * 100))

def map_trigger_to_100(value):
    """
    Mapea el valor del gatillo (típicamente -1.0 a 1.0) al rango 0 a 100.
    Esto es para el control progresivo del arma.
    """
    # Normalizamos a 0-1 (0 suelto, 1 presionado) y luego a 0-100.
    normalized = (value + 1.0) / 2.0
    # Aseguramos que sea un entero entre 0 y 100
    return int(round(normalized * 100))

def get_gamepad_state(joystick):
    """
    Lee todos los estados relevantes del gamepad y los normaliza.
    """
    # Heartbeat (contador creciente)
    global heartbeat_counter
    heartbeat_counter += 1
    if heartbeat_counter > 255:
        heartbeat_counter = 0

    # Lectura de ejes
    ly_axis_raw = joystick.get_axis(AXIS_LEFT_Y)
    lx_axis_raw = joystick.get_axis(AXIS_LEFT_X)
    r_trigger_raw = joystick.get_axis(AXIS_R_TRIGGER)
    
    # 1. INVERSIÓN CRÍTICA: El eje Y se invierte.
    # Así, si empujas el joystick IZQUIERDO hacia adelante (raw=-1.0),
    # LY_Axis se convierte en 100 (Avance Max).
    ly_axis = map_axis_to_100(-ly_axis_raw)
    
    # El eje X no necesita inversión
    lx_axis = map_axis_to_100(lx_axis_raw)
    
    # El gatillo derecho
    r_trigger = map_trigger_to_100(r_trigger_raw)

    # Lectura del botón Kill Switch
    button_b = joystick.get_button(BUTTON_KILL_SWITCH)
    
    # Estructura de datos a enviar: 5 bytes
    # Formato: {int8_t lyAxis, int8_t lxAxis, int8_t rTrigger, uint8_t buttonB, uint8_t heartbeat}
    # '<bbB B B' define los tipos de datos exactos que espera el ESP32:
    # b = signed char (int8_t), B = unsigned char (uint8_t)
    packed_data = struct.pack(
        '<bbB B B', 
        ly_axis,
        lx_axis,
        r_trigger,
        button_b,
        heartbeat_counter
    )
    
    return packed_data, ly_axis, lx_axis, r_trigger, button_b

def main():
    """
    Bucle principal de lectura y envío de datos.
    """
    try:
        # 1. Inicializar Joystick
        if pygame.joystick.get_count() == 0:
            print("ERROR: No se detectó ningún gamepad.")
            print("Asegúrate de que el Logitech F310 esté conectado y encendido.")
            sys.exit(1)

        joystick = pygame.joystick.Joystick(0)
        joystick.init()
        print(f"Gamepad detectado: {joystick.get_name()}")
        print(f"Conectando a {TARGET_IP}:{TARGET_PORT} (Frecuencia: {1/SEND_INTERVAL:.0f} Hz)...")

        # 2. Inicializar Socket UDP
        udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # 3. Bucle de envío
        last_send_time = time.time()
        
        while True:
            # Procesar eventos de Pygame para mantener la conexión activa
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    raise KeyboardInterrupt

            current_time = time.time()
            if current_time - last_send_time >= SEND_INTERVAL:
                # Obtener estado del gamepad
                packed_data, ly, lx, rt, bB = get_gamepad_state(joystick)
                
                # Enviar paquete UDP
              #  udp_socket.sendto(packed_data, (TARGET_IP, TARGET_PORT))
                
                # Mostrar el estado actual (opcional)
                # Usamos end='\r' para actualizar la línea en lugar de imprimir una nueva.
                print(f"Enviado: LY={ly:<4} LX={lx:<4} RT={rt:<4} B_Kill={bB:<1} | Heartbeat: {heartbeat_counter}", end='\r')

                last_send_time = current_time

    except KeyboardInterrupt:
        print("\n\nControlador detenido por el usuario.")
    except Exception as e:
        print(f"\nSe ha producido un error: {e}")
    finally:
        # Limpieza
        pygame.joystick.quit()
        pygame.quit()
        print("Fin del programa.")


# Contador de paquetes global
heartbeat_counter = 0

if __name__ == '__main__':
    main()