#include <WiFi.h>
#include <WiFiUdp.h>

// --- Definición de Pines GPIO para el TB6612FNG (Motor Driver) ---
// Motor A (Derecho)
const int AIN1 = 19; 
const int AIN2 = 18;
const int PWMA = 5;  // Pin para PWM (Velocidad)
// Motor B (Izquierdo)
const int BIN1 = 17;
const int BIN2 = 16;
const int PWMB = 4;  // Pin para PWM (Velocidad)
// Pin STBY (Standby)
const int STBY = 2; // Controla si el driver está activo

// --- Definición de Pines GPIO para los Relés (Discos Giratorios) ---
const int RELAY_DISCO_1 = 15; // Motor Lector CD 1
const int RELAY_DISCO_2 = 13; // Motor Lector CD 2

// --- Configuración de PWM para el ESP32 ---
constexpr int PWM_FREQ = 5000;      // Frecuencia del PWM (5 kHz)
constexpr int PWM_RESOLUTION = 10;  // Resolución de 10 bits (0-1023)
constexpr int MAX_DUTY_CYCLE = (1 << PWM_RESOLUTION) - 1; // 1023

// --- Configuración de Protección de Voltaje ---
// ⚠️ IMPORTANTE: Motores de 6V con batería de 12V
constexpr float BATTERY_VOLTAGE = 12.0;  // Voltaje de la batería
constexpr float MOTOR_VOLTAGE = 6.0;     // Voltaje nominal de los motores
constexpr float VOLTAGE_RATIO = MOTOR_VOLTAGE / BATTERY_VOLTAGE; // 6V/12V = 0.5 (50%)

// PWM máximo permitido para no exceder el voltaje del motor
// Con 12V de batería y motores de 6V, limitamos al 50% del PWM
  
  // 5. Mostrar información de protección de voltaje
  Serial.println("\n=== PROTECCIÓN DE VOLTAJE ===");
  Serial.printf("Batería: %.1fV | Motores: %.1fV\n", BATTERY_VOLTAGE, MOTOR_VOLTAGE);
  Serial.printf("Ratio de voltaje: %.0f%%\n", VOLTAGE_RATIO * 100);
  Serial.printf("PWM máximo permitido: %d/%d\n", MAX_SAFE_PWM, MAX_DUTY_CYCLE);
  Serial.println("✅ Los motores están protegidos contra sobrevoltaje.\n");
  
  // Inicializar timestamp
  lastCommandTime = millis();
}

void loop() {
  // 1. Verificar si hay un paquete UDP disponible
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    // Leer el paquete
    int len = Udp.read(packetBuffer, 255);
    packetBuffer[len] = 0; // Terminar la cadena
    
    // 2. Parsear el comando TANK DRIVE (Formato: "L:motorLeft,R:motorRight,B1:btn1,B2:btn2")
    if (sscanf(packetBuffer, "L:%d,R:%d,B1:%d,B2:%d", &motorLeft, &motorRight, &btnDisco1, &btnDisco2) == 4) {
      
      // Validar rangos recibidos
      motorLeft = constrain(motorLeft, -100, 100);
      motorRight = constrain(motorRight, -100, 100);
      btnDisco1 = constrain(btnDisco1, 0, 1);
      btnDisco2 = constrain(btnDisco2, 0, 1);
      
      // Actualizar timestamp de último comando válido
      lastCommandTime = millis();

      // --- TANK DRIVE: Control Directo de Motores ---
      // Cada valor controla directamente un motor, sin mezcla
      // ⚠️ IMPORTANTE: Usamos MAX_SAFE_PWM (511) en lugar de MAX_DUTY_CYCLE (1023)
      // para limitar el voltaje efectivo a 6V (50% de 12V)
      int speedLeft = map(motorLeft, -100, 100, -MAX_SAFE_PWM, MAX_SAFE_PWM);
      int speedRight = map(motorRight, -100, 100, -MAX_SAFE_PWM, MAX_SAFE_PWM);
      
      // Aplicar control a los motores (setMotor ya aplica constrain adicional)
      setMotor(PWMB, BIN1, BIN2, speedLeft);  // Motor B (Izquierdo)
      setMotor(PWMA, AIN1, AIN2, speedRight); // Motor A (Derecho)
      
      // --- Lógica de Control de Discos Giratorios ---
      controlDiscos(btnDisco1, btnDisco2);
      
      // Debug: Mostrar valores (opcional, comentar si no se necesita)
      Serial.printf("L:%d R:%d | PWM_L:%d PWM_R:%d (max:%d) | B1:%d B2:%d\n", 
                    motorLeft, motorRight, speedLeft, speedRight, MAX_SAFE_PWM, btnDisco1, btnDisco2);
    }
  } 
  
  // 3. TIMEOUT DE SEGURIDAD: Detener motores si no hay comandos recientes
  if (millis() - lastCommandTime > TIMEOUT_MS) {
    stopAllMotors();
    // Solo imprimir una vez cuando se active el timeout
    static bool timeoutWarningShown = false;
    if (!timeoutWarningShown) {
      Serial.println("⚠️ TIMEOUT: Sin comandos, motores detenidos por seguridad");
      timeoutWarningShown = true;
    }
  } else {
    // Resetear la advertencia cuando vuelvan los comandos
    static bool timeoutWarningShown = false;
    timeoutWarningShown = false;
  }
}