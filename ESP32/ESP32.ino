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
constexpr int MAX_SAFE_PWM = (int)(MAX_DUTY_CYCLE * VOLTAGE_RATIO); // 1023 * 0.5 = 511

// --- Configuración de Seguridad ---
constexpr unsigned long TIMEOUT_MS = 500; // Timeout de seguridad (500ms sin comandos = detener)
unsigned long lastCommandTime = 0;

// --- Configuración de Red Wi-Fi y UDP ---
const char* ssid = "DIOSGRACIAS";      // Reemplaza con el nombre de tu red
const char* password = "123456789"; // Reemplaza con tu contraseña

// Configuración de IP estática
IPAddress local_IP(192, 168, 0, 111);    // IP estática deseada
IPAddress gateway(192, 168, 0, 1);      // Gateway (router)
IPAddress subnet(255, 255, 255, 0);     // Máscara de subred
IPAddress primaryDNS(8, 8, 8, 8);       // DNS primario (Google DNS)
IPAddress secondaryDNS(8, 8, 4, 4);     // DNS secundario (Google DNS)

constexpr unsigned int LOCAL_PORT = 2390;    // Puerto para escuchar comandos
char packetBuffer[255];                 // Buffer para los datos recibidos
WiFiUDP Udp;

// --- Variables de Estado - TANK DRIVE ---
int motorLeft = 0;   // Valor del motor izquierdo (-100 a 100)
int motorRight = 0;  // Valor del motor derecho (-100 a 100)
int btnDisco1 = 0;   // Botón para Disco 1
int btnDisco2 = 0;   // Botón para Disco 2


// =======================================================================
// === Funciones de Control del Vehículo ===
// =======================================================================

// Inicializa los canales PWM
void setupPWM() {
  // En ESP32 Core v3.0: ledcAttach(pin, freq, resolution);
  ledcAttach(PWMA, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(PWMB, PWM_FREQ, PWM_RESOLUTION);
}

// Controla la velocidad y dirección de los motorreductores
void setMotor(int pwmPin, int dirPin1, int dirPin2, int speed) {
  // Asegura que el driver esté activo (STBY en HIGH)
  digitalWrite(STBY, HIGH); 
  
  // Limita la velocidad al PWM seguro (protección de voltaje)
  speed = constrain(speed, -MAX_SAFE_PWM, MAX_SAFE_PWM);
  
  // Si la velocidad es positiva, ir hacia adelante
  if (speed > 0) {
    digitalWrite(dirPin1, HIGH);
    digitalWrite(dirPin2, LOW);
    ledcWrite(pwmPin, speed);
  } 
  // Si la velocidad es negativa, ir hacia atrás
  else if (speed < 0) {
    digitalWrite(dirPin1, LOW);
    digitalWrite(dirPin2, HIGH);
    // Usamos el valor absoluto para PWM, ya que el signo indica la dirección
    ledcWrite(pwmPin, abs(speed));
  } 
  // Si la velocidad es cero, detener el motor
  else {
    digitalWrite(dirPin1, LOW);
    digitalWrite(dirPin2, LOW);
    ledcWrite(pwmPin, 0);
  }
}

// Detiene todos los motores (función de seguridad)
void stopAllMotors() {
  setMotor(PWMA, AIN1, AIN2, 0);
  setMotor(PWMB, BIN1, BIN2, 0);
  digitalWrite(STBY, LOW); // Poner driver en standby
}

// =======================================================================
// === Funciones de Control de Discos Giratorios ===
// =======================================================================

void controlDiscos(int btn1, int btn2) {
  // Controla el primer motor con el relé 1
  // Asumimos un módulo de relé que se ACTIVA con HIGH (depende de tu módulo)
  digitalWrite(RELAY_DISCO_1, btn1 == 1 ? HIGH : LOW);

  // Controla el segundo motor con el relé 2
  digitalWrite(RELAY_DISCO_2, btn2 == 1 ? HIGH : LOW);

  // NOTA: Si tu módulo de relé se activa con LOW, invierte la lógica:
  // digitalWrite(RELAY_DISCO_1, btn1 == 1 ? LOW : HIGH);
}

// =======================================================================
// === Setup y Loop de Arduino ===
// =======================================================================

void setup() {
  Serial.begin(115200);

  // 1. Configuración de Pines
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  
  pinMode(RELAY_DISCO_1, OUTPUT);
  pinMode(RELAY_DISCO_2, OUTPUT);
  
  // Inicializa motores apagados y relés en OFF
  digitalWrite(STBY, LOW); 
  digitalWrite(RELAY_DISCO_1, LOW);
  digitalWrite(RELAY_DISCO_2, LOW);
  
  // 2. Configuración de PWM
  setupPWM();

  // 3. Conexión Wi-Fi con IP estática
  Serial.println("\n=== CONFIGURACIÓN DE RED ===");
  Serial.printf("IP estática solicitada: %s\n", local_IP.toString().c_str());
  Serial.printf("Gateway: %s\n", gateway.toString().c_str());
  
  // Configurar IP estática ANTES de conectar
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("⚠️ Error al configurar IP estática");
  }
  
  Serial.printf("\nConectando a %s ", ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ¡Conectado!");
    Serial.print("IP asignada: ");
    Serial.println(WiFi.localIP());
    
    if (WiFi.localIP() == local_IP) {
      Serial.println("✅ IP estática configurada correctamente");
    } else {
      Serial.println("⚠️ IP diferente a la solicitada (verifica configuración del router)");
    }
  } else {
    Serial.println(" ❌ Error de conexión");
    Serial.println("Verifica SSID y contraseña");
  }

  // 4. Iniciar Servidor UDP
  Udp.begin(LOCAL_PORT);
  Serial.print("Escuchando comandos UDP en el puerto: ");
  Serial.println(LOCAL_PORT);
  Serial.println("Modo: TANK DRIVE (control independiente de motores)");
  
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