import pygame
import socket
import time

# --- Configuración de Red UDP ---
# Dirección IP del ESP32 (Asegúrate que coincida con la IP que mostró el ESP32)
ESP32_IP = "192.168.1.100"  # ¡Cámbiala a la IP real de tu ESP32!
ESP32_PORT = 2390          # Debe coincidir con el puerto en el código del ESP32

# --- Configuración del Joystick ---
# Rango de valores de los ejes (0.0 a 1.0 o -1.0 a 1.0)
JOYSTICK_DEADZONE = 0.1 # Ignorar movimientos pequeños cerca del centro

# Inicializa pygame
pygame.init()
pygame.joystick.init()

# 1. Verificar Joystick
if pygame.joystick.get_count() == 0:
    print("❌ No se detectó ningún Joystick. Conecta el control de Logitech y reinicia.")
    pygame.quit()
    exit()

# Selecciona el primer joystick detectado
joystick = pygame.joystick.Joystick(0)
joystick.init()

print(f"✅ Joystick detectado: {joystick.get_name()}")
print("🚀 Iniciando conexión UDP...")

# Inicializa el socket UDP
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bucle principal de control
try:
    while True:
        # Procesar eventos de pygame (necesario para actualizar el estado del joystick)
        pygame.event.pump()
        
        # --- 2. Lectura de Ejes (Para el control del vehículo) ---
        
        # Asumiendo un joystick de tipo gamepad:
        # Eje 0: Movimiento Horizontal (X)
        # Eje 1: Movimiento Vertical (Y)
        
        # Lee los valores de los ejes. El valor estará entre -1.0 y 1.0
        # NOTA: En muchos joysticks, Y es -1.0 (arriba) a 1.0 (abajo).
        # Lo invertiremos para que 'arriba' sea positivo (+100)
        
        # Leer Eje Y (Adelante/Atrás)
        try:
            raw_y = -joystick.get_axis(1) # Invertimos el signo
        except:
            raw_y = 0
            
        # Leer Eje X (Giro)
        try:
            raw_x = joystick.get_axis(0)
        except:
            raw_x = 0

        # Aplicar Deadzone y mapear a rango -100 a 100
        # Eje Y: Adelante/Atrás
        if abs(raw_y) < JOYSTICK_DEADZONE:
            joy_y = 0
        else:
            # Mapeo a un rango simple de -100 a 100 para simplificar el parseo en el ESP32
            joy_y = int(raw_y * 100)
            
        # Eje X: Giro Izquierda/Derecha
        if abs(raw_x) < JOYSTICK_DEADZONE:
            joy_x = 0
        else:
            joy_x = int(raw_x * 100)


        # --- 3. Lectura de Botones (Para los discos giratorios) ---
        
        # Puedes cambiar estos índices (0, 1, 2, etc.) según cómo
        # tu joystick Logitech mapee los botones (A, B, X, Y, Triggers, etc.)
        # El índice 0 puede ser el botón 'A' o 'X' en un gamepad.
        
        # Botón para Disco 1
        btn_disco_1 = joystick.get_button(0) # Ejemplo: Botón 0
        
        # Botón para Disco 2
        btn_disco_2 = joystick.get_button(1) # Ejemplo: Botón 1

        # --- 4. Crear el Paquete de Datos ---
        # Formato: "Y:joyY,X:joyX,B1:btn1,B2:btn2"
        data_packet = f"Y:{joy_y},X:{joy_x},B1:{btn_disco_1},B2:{btn_disco_2}"

        # --- 5. Enviar el Paquete UDP ---
        sock.sendto(data_packet.encode(), (ESP32_IP, ESP32_PORT))
        
        # Opcional: Mostrar el paquete enviado
        print(f"Enviando: {data_packet}       ", end='\r')

        # Controlar la frecuencia de envío (para no saturar la red ni al ESP32)
        # 50 Hz (20 milisegundos) es una buena frecuencia para control en tiempo real.
        time.sleep(0.02) 

except KeyboardInterrupt:
    print("\nDeteniendo el controlador...")
finally:
    # Asegúrate de cerrar todo limpiamente
    sock.close()
    pygame.quit()
    print("Programa terminado.")