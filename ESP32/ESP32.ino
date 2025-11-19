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

// --- Configuración de Seguridad ---
constexpr unsigned long TIMEOUT_MS = 500; // Timeout de seguridad (500ms sin comandos = detener)
unsigned long lastCommandTime = 0;

// --- Configuración de Red Wi-Fi y UDP ---
const char* ssid = "Tu_Red_WiFi";      // Reemplaza con el nombre de tu red
const char* password = "Tu_Contrasena"; // Reemplaza con tu contraseña
constexpr unsigned int LOCAL_PORT = 2390;    // Puerto para escuchar comandos
char packetBuffer[255];                 // Buffer para los datos recibidos
WiFiUDP Udp;

// --- Variables de Estado ---
int joyY = 0;   // Valor del eje Y del joystick (Adelante/Atrás)
int joyX = 0;   // Valor del eje X del joystick (Giro)
int btnDisco1 = 0; // Botón para Disco 1
int btnDisco2 = 0; // Botón para Disco 2


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
  
  // Limita la velocidad entre -1023 y 1023
  speed = constrain(speed, -MAX_DUTY_CYCLE, MAX_DUTY_CYCLE);
  
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

  // 3. Conexión Wi-Fi
  Serial.printf("Conectando a %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" ¡Conectado!");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // 4. Iniciar Servidor UDP
  Udp.begin(LOCAL_PORT);
  Serial.print("Escuchando comandos UDP en el puerto: ");
  Serial.println(LOCAL_PORT);
  
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
    
    // 2. Parsear el comando (Formato esperado: "Y:joyY,X:joyX,B1:btn1,B2:btn2")
    if (sscanf(packetBuffer, "Y:%d,X:%d,B1:%d,B2:%d", &joyY, &joyX, &btnDisco1, &btnDisco2) == 4) {
      
      // Validar rangos recibidos
      joyY = constrain(joyY, -100, 100);
      joyX = constrain(joyX, -100, 100);
      btnDisco1 = constrain(btnDisco1, 0, 1);
      btnDisco2 = constrain(btnDisco2, 0, 1);
      
      // Actualizar timestamp de último comando válido
      lastCommandTime = millis();

      // --- Lógica de Control del Vehículo (Modelo Diferencial) ---
      // Mapear los valores del joystick (-100 a 100) al rango PWM (-1023 a 1023)
      int forwardSpeed = map(joyY, -100, 100, -MAX_DUTY_CYCLE, MAX_DUTY_CYCLE);
      int turnAmount = map(joyX, -100, 100, -MAX_DUTY_CYCLE, MAX_DUTY_CYCLE); 
      
      // Velocidad de cada motor (usando un modelo de mezcla simple)
      int speedLeft  = forwardSpeed + turnAmount;
      int speedRight = forwardSpeed - turnAmount;
      
      // IMPORTANTE: Limitar velocidades combinadas para evitar saturación
      speedLeft = constrain(speedLeft, -MAX_DUTY_CYCLE, MAX_DUTY_CYCLE);
      speedRight = constrain(speedRight, -MAX_DUTY_CYCLE, MAX_DUTY_CYCLE);
      
      // Aplicar control a los motores
      setMotor(PWMB, BIN1, BIN2, speedLeft);  // Motor B (Izquierdo)
      setMotor(PWMA, AIN1, AIN2, speedRight); // Motor A (Derecho)
      
      // --- Lógica de Control de Discos Giratorios ---
      controlDiscos(btnDisco1, btnDisco2);
      
      // Debug: Mostrar valores (opcional, comentar si no se necesita)
      Serial.printf("Y:%d X:%d | L:%d R:%d | B1:%d B2:%d\n", 
                    joyY, joyX, speedLeft, speedRight, btnDisco1, btnDisco2);
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