#include <WiFi.h>
#include <AsyncUDP.h>
#include <Arduino.h> // Incluye funciones como constrain() y max()

// --- 1. CONFIGURACIÓN WIFI ---
const char* ssid = "TU_NOMBRE_WIFI";       // Reemplaza con el nombre de tu red
const char* password = "TU_CONTRASEÑA_WIFI"; // Reemplaza con tu contraseña
const int PORT = 4200;                     // Puerto UDP de escucha
AsyncUDP udp;

// --- 2. CONFIGURACIÓN DEL SISTEMA DE SEGURIDAD Y TIMEOUT ---
const int KILL_PIN = 23;      // Pin GPIO conectado al relé de seguridad
unsigned long lastPacketTime = 0;
const int TIMEOUT_MS = 500;   // 500ms sin recibir paquete = FAIL-SAFE

// --- 3. CONFIGURACIÓN DE PINES Y CANALES PWM DEL MOTOR (Unidireccional) ---

// Configuración general del PWM del ESP32
const int freq = 5000;      // Frecuencia en Hz
const int resolution = 8;   // Resolución de 8 bits (0-255)

// DRIVETRAIN - Motor Izquierdo (M1) - SOLO USA PIN PWM
const int M1_PWM_CHANNEL = 0;
const int M1_PIN_PWM = 2;   // GPIO para PWM

// DRIVETRAIN - Motor Derecho (M2) - SOLO USA PIN PWM
const int M2_PWM_CHANNEL = 1;
const int M2_PIN_PWM = 17;  // GPIO para PWM

// ARMA/ROTOR - Motor Arma (M3) - SOLO USA PIN PWM
const int M3_PWM_CHANNEL = 2;
const int M3_PIN_PWM = 21;  // GPIO para PWM
// Nota: El pin M3_PIN_DIR (22) del código anterior ya no se usa, ya que la dirección es fija.


// --- 4. ESTRUCTURA DE DATOS RECIBIDOS (5 bytes) ---
struct ControlData {
    int8_t lyAxis;    // Eje Y Izquierdo (-100 a 100)
    int8_t lxAxis;    // Eje X Izquierdo (-100 a 100)
    int8_t rTrigger;  // Gatillo Derecho (0 a 100)
    uint8_t buttonB;  // Botón B (Kill Switch - 0 o 1)
    uint8_t heartbeat; // Contador
};


// --- DECLARACIONES DE FUNCIONES ---
void activateKillSwitch(bool state);
void stopAllMotors();
// Firma simplificada: solo necesita el canal y el valor PWM
void setMotorSpeed(int pwmChannel, int pwmValue); 
void handleUdpPacket(AsyncUDPPacket packet);


void setup() {
    Serial.begin(115200);

    // 4.1 Configuración de PWM (LEDC)
    ledcSetup(M1_PWM_CHANNEL, freq, resolution);
    ledcSetup(M2_PWM_CHANNEL, freq, resolution);
    ledcSetup(M3_PWM_CHANNEL, freq, resolution);

    ledcAttachPin(M1_PIN_PWM, M1_PWM_CHANNEL);
    ledcAttachPin(M2_PIN_PWM, M2_PWM_CHANNEL);
    ledcAttachPin(M3_PIN_PWM, M3_PWM_CHANNEL);

    // 4.2 Configuración de pines de Seguridad
    pinMode(KILL_PIN, OUTPUT);

    // Inicialmente, habilitar energía (Modo NORMAL) y detener motores
    digitalWrite(KILL_PIN, HIGH);
    stopAllMotors();

    // 4.3 Inicialización Wi-Fi (Modo Cliente)
    WiFi.begin(ssid, password);
    Serial.print("Conectando a Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n¡Conectado!");
    Serial.print("IP del ESP32: ");
    Serial.println(WiFi.localIP());

    // 4.4 Inicialización UDP
    if (udp.listen(PORT, handleUdpPacket)) {
        Serial.print("Escuchando paquetes UDP en el puerto ");
        Serial.println(PORT);
    } else {
        Serial.println("Error al iniciar UDP.");
    }
}

void loop() {
    // Monitoreo del Fail-Safe (Paso 1 del bucle principal)
    if (millis() - lastPacketTime > TIMEOUT_MS) {
        if (digitalRead(KILL_PIN) == HIGH) { // Sólo si ya está activo
            Serial.println("¡Alerta! Pérdida de comunicación - FAIL-SAFE ACTIVADO");
            activateKillSwitch(true); // Activar Kill Switch (corte de energía)
        }
    }
}

/**
 * @brief Función de Callback que se ejecuta al recibir un paquete UDP.
 */
void handleUdpPacket(AsyncUDPPacket packet) {
    lastPacketTime = millis();
    activateKillSwitch(false); // Deshabilitar Kill Switch si estaba activo

    if (packet.length() < sizeof(ControlData)) {
        Serial.println("Paquete UDP incompleto o corrupto.");
        return;
    }

    ControlData* data = (ControlData*)packet.data();

    int LY_Axis = data->lyAxis;
    int LX_Axis = data->lxAxis;
    int R_Trigger = data->rTrigger;
    int Button_B = data->buttonB;

    // 2. Ejecutar Apagado de Emergencia Remoto
    if (Button_B == 1) {
        Serial.println("Apagado Remoto Activo (Gamepad)");
        activateKillSwitch(true); // Activa Kill Switch y detiene motores
        return;
    }

    // 3. Control de Movilidad (Giro Diferencial - Tank-Drive Mix)
    
    // Paso 1: Usar solo la parte positiva de LY_Axis (solo avance).
    int forwardSpeed = max(0, LY_Axis); // Rango de 0 a 100

    // Paso 2: Cálculo de la mezcla de velocidad:
    int rawLeftSpeed = forwardSpeed + LX_Axis;
    int rawRightSpeed = forwardSpeed - LX_Axis;

    // Paso 3: Asegurar que la velocidad se mantenga en el rango de 0 a 100
    int leftMotorSpeed = constrain(rawLeftSpeed, 0, 100);
    int rightMotorSpeed = constrain(rawRightSpeed, 0, 100);
    
    // Paso 4: Convertir el rango (0 a 100) a (0 a 255) para el PWM
    int leftPWM = map(leftMotorSpeed, 0, 100, 0, 255);
    int rightPWM = map(rightMotorSpeed, 0, 100, 0, 255);

    // Aplicar la velocidad a los motores de tracción
    setMotorSpeed(M1_PWM_CHANNEL, leftPWM);
    setMotorSpeed(M2_PWM_CHANNEL, rightPWM);

    // 4. Control de Arma
    // R_Trigger (0 a 100) se mapea a PWM (0-255).
    int weaponPWM = map(R_Trigger, 0, 100, 0, 255);
    ledcWrite(M3_PWM_CHANNEL, weaponPWM);
}

/**
 * @brief Implementa el corte físico de energía a través del relé.
 */
void activateKillSwitch(bool state) {
    if (state) {
        // Corte de energía total (apaga el relé)
        digitalWrite(KILL_PIN, LOW);
        stopAllMotors(); // Redundancia
    } else {
        // Habilitar energía (enciende el relé)
        digitalWrite(KILL_PIN, HIGH);
    }
}

/**
 * @brief Detiene instantáneamente todos los motores (PWM a 0).
 */
void stopAllMotors() {
    ledcWrite(M1_PWM_CHANNEL, 0);
    ledcWrite(M2_PWM_CHANNEL, 0);
    ledcWrite(M3_PWM_CHANNEL, 0);
}

/**
 * @brief Establece la velocidad de un motor DC unidireccional (solo PWM).
 * @param pwmChannel Canal PWM
 * @param pwmValue Valor PWM en el rango de 0 (Parado) a 255 (Máx).
 */
void setMotorSpeed(int pwmChannel, int pwmValue) {
    // Limitar el valor PWM a un rango válido (0 a 255)
    pwmValue = constrain(pwmValue, 0, 255); 
    
    // Escribir el valor PWM al motor
    ledcWrite(pwmChannel, pwmValue);
}
```eof

Este código es la versión más simplificada y cumple con:

* **Sin Puente H:** La función `setMotorSpeed` solo utiliza la salida PWM para controlar la velocidad.
* **Unidireccional:** La lógica en `handleUdpPacket` fuerza que la velocidad sea siempre de **0 o positiva** (`forwardSpeed = max(0, LY_Axis)`), garantizando solo avance.
* **Seguridad:** Mantiene el Fail-Safe por timeout y el Kill Switch físico.

Ahora puedes cargar este código a tu ESP32 y usar el script de PC para controlarlo.
