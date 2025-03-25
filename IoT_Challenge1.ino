#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Definición de pines
#define ONE_WIRE_BUS 13      // Sensor de temperatura DS18B20
#define BUZZER_PIN 12        // Buzzer (lógica inversa: LOW activa)
#define FLAME_SENSOR_PIN 32  // Sensor de llama (se asume que LOW indica llama)
#define GAS_SENSOR_PIN 34    // Sensor de gas MQ-2
#define RGB_RED_PIN 33       // LED RGB – componente rojo
#define RGB_GREEN_PIN 25     // LED RGB – componente verde
#define RGB_BLUE_PIN 26      // LED RGB – componente azul

// Configuración de la LCD (dirección 0x27, 16 columnas, 2 filas)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Configuración de sensores
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Datos WiFi y servidor
const char* ssid = "juanpa";
const char* password = "Juan1234";
WiFiServer server(80);

// Variables de sensores y estado
float tempC = 0.0;
int gasValue = 0, flameValue = 0;
bool alertaMedia = false, alertaMediaAlta = false, incendio = false;
String razonAlerta = "Normal";
bool flameDetected = false;  // true si se detecta llama

// Valores iniciales (tomados al inicio)
float tempInicial = 0.0;
int gasInicial = 0;

// Control de dispositivos (se pueden apagar/encender vía web)
bool lcdOn = true, buzzerGlobalOn = true, rgbOn = true;

// Registro de los últimos 10 datos
#define MAX_REGISTROS 10
String registros[MAX_REGISTROS];
int registroIndex = 0;

// Variables para patrón del buzzer
unsigned long lastBuzzerToggle = 0;
bool buzzerOnState = false;

void actualizarEstado() {
  sensors.requestTemperatures();
  tempC = sensors.getTempCByIndex(0);
  gasValue = analogRead(GAS_SENSOR_PIN);
  flameValue = digitalRead(FLAME_SENSOR_PIN);
  flameDetected = (flameValue == LOW);  // LOW indica que hay llama

  // Reiniciar condiciones
  razonAlerta = "Normal";
  alertaMedia = alertaMediaAlta = incendio = false;

  // Evaluar condiciones de alerta (basado en incremento respecto a los valores iniciales)
  if (tempC >= tempInicial + 5 || gasValue >= gasInicial + 100 || flameDetected) {
    alertaMediaAlta = true;
    razonAlerta = "Alerta alta: ";
    if (tempC >= tempInicial + 5) { razonAlerta += "Temp alta "; }
    if (gasValue >= gasInicial + 100) { razonAlerta += "Gas alto "; }
    if (flameDetected) { razonAlerta += "Llama detectada"; }
    // Si se superan umbrales mayores, se considera incendio
    if (tempC >= tempInicial + 7 || gasValue >= gasInicial + 150) {
      incendio = true;
      razonAlerta = "INCENDIO DETECTADO";
    }
  } else if (tempC >= tempInicial + 3 || gasValue >= gasInicial + 50) {
    alertaMedia = true;
    razonAlerta = "Alerta media: ";
    if (tempC >= tempInicial + 3) { razonAlerta += "Temp moderada "; }
    if (gasValue >= gasInicial + 50) { razonAlerta += "Gas moderado"; }
  }
}

void actualizarLCD() {
  if (lcdOn) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(tempC);
    lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Gas: ");
    lcd.print(gasValue);
    lcd.setCursor(10, 1);
    lcd.print(flameDetected ? "LLA" : "OK");  // LLA indica llama detectada
  } else {
    lcd.clear();
  }
}

void actualizarRGB() {
  // Se invierten los colores: nuevo = 255 - original
  if (rgbOn) {
    if (incendio) {
      // Normal: (255,0,0) -> invertido: (0,255,255)
      analogWrite(RGB_RED_PIN, 255 - 255);  // 0
      analogWrite(RGB_GREEN_PIN, 255 - 0);  // 255
      analogWrite(RGB_BLUE_PIN, 255 - 0);   // 255
    } else if (alertaMediaAlta) {
      // Normal: (255,165,0) -> invertido: (0,90,255)
      analogWrite(RGB_RED_PIN, 255 - 255);    // 0
      analogWrite(RGB_GREEN_PIN, 255 - 165);  // 90
      analogWrite(RGB_BLUE_PIN, 255 - 0);     // 255
    } else if (alertaMedia) {
      // Normal: (255,255,0) -> invertido: (0,0,255)
      analogWrite(RGB_RED_PIN, 255 - 255);    // 0
      analogWrite(RGB_GREEN_PIN, 255 - 255);  // 0
      analogWrite(RGB_BLUE_PIN, 255 - 0);     // 255
    } else {
      // Normal: (0,255,0) -> invertido: (255,0,255)
      analogWrite(RGB_RED_PIN, 255 - 0);      // 255
      analogWrite(RGB_GREEN_PIN, 255 - 255);  // 0
      analogWrite(RGB_BLUE_PIN, 255 - 0);     // 255
    }
  } else {
    // Si se apaga el RGB, establecemos el color blanco (255, 255, 255)
    analogWrite(RGB_RED_PIN, 0);    // Blanco: Rojo al máximo
    analogWrite(RGB_GREEN_PIN, 0);  // Blanco: Verde al máximo
    analogWrite(RGB_BLUE_PIN, 0);   // Blanco: Azul al máximo
  }
}

void actualizarBuzzer() {
  // Patrones de sonido: si hay incendio, buzzer continuo; alerta alta: pitido cada 2s; alerta media: cada 4s.
  unsigned long currentMillis = millis();

  if (!buzzerGlobalOn || !(incendio || alertaMediaAlta || alertaMedia)) {
    digitalWrite(BUZZER_PIN, HIGH);  // Apagado (lógica inversa)
    return;
  }

  if (incendio) {
    // Beep continuo
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    unsigned long period = (alertaMediaAlta) ? 2000UL : 4000UL;  // 2s o 4s
    unsigned long beepDuration = 500UL;                          // 500 ms de pitido
    // Si se pasó el periodo, reiniciamos el contador
    if (currentMillis - lastBuzzerToggle >= period) {
      lastBuzzerToggle = currentMillis;
      buzzerOnState = true;
    }
    // Durante beepDuration, buzzer activo, luego apagado
    if (buzzerOnState && (currentMillis - lastBuzzerToggle < beepDuration)) {
      digitalWrite(BUZZER_PIN, LOW);
    } else {
      digitalWrite(BUZZER_PIN, HIGH);
    }
  }
}

void almacenarRegistro() {
  String reg = "Temp: " + String(tempC) + "C, Gas: " + String(gasValue) + ", Llama: " + (flameDetected ? "SI" : "NO") + ", Estado: " + razonAlerta;
  registros[registroIndex] = reg;
  registroIndex = (registroIndex + 1) % MAX_REGISTROS;
}

void sensorTask(void* pvParameters) {
  // Guardar valores iniciales
  sensors.requestTemperatures();
  tempInicial = sensors.getTempCByIndex(0);
  gasInicial = analogRead(GAS_SENSOR_PIN);

  while (1) {
    actualizarEstado();
    actualizarLCD();
    actualizarRGB();
    actualizarBuzzer();
    almacenarRegistro();

    // Imprimir en consola
    Serial.print("Temp: ");
    Serial.print(tempC);
    Serial.print("C, Gas: ");
    Serial.print(gasValue);
    Serial.print(", Llama: ");
    Serial.print(flameDetected ? "SI" : "NO");
    Serial.print(", Estado: ");
    Serial.println(razonAlerta);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Iniciado");
  delay(2000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);

  // Apagar buzzer por defecto (HIGH inactiva)
  digitalWrite(BUZZER_PIN, HIGH);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  server.begin();
  sensors.begin();

  xTaskCreate(sensorTask, "SensorTask", 4096, NULL, 1, NULL);
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    // Comandos para controlar dispositivos
    if (request.indexOf("/offLCD") != -1) lcdOn = false;
    if (request.indexOf("/onLCD") != -1) lcdOn = true;
    if (request.indexOf("/offBuzzer") != -1) buzzerGlobalOn = false;
    if (request.indexOf("/onBuzzer") != -1) buzzerGlobalOn = true;
    if (request.indexOf("/offRGB") != -1) rgbOn = false;
    if (request.indexOf("/onRGB") != -1) rgbOn = true;

    // Respuesta HTTP con valores actuales y tabla de registros
    client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"));
    client.print(F("<!DOCTYPE html><html lang='es'><head>"
                   "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                   "<title>ESP32 Sensor Data</title>"
                   "<style>"
                   "*{margin:0;padding:0;box-sizing:border-box;}"
                   "body{font-family:'Segoe UI',Arial,sans-serif;height:100vh;width:100%;"
                   "background:linear-gradient(to bottom,#f8ffe8 0%,#e3f5ab 33%,#b7df2d 100%);color:white;text-align:center;}"
                   ".info-container{margin-top:20px;background:rgba(0,0,0,0.093);color:white;display:flex;"
                   "width:fit-content;flex-direction:column;justify-self:center;padding:10px;border-radius:10px;"
                   "text-shadow:1px 1px 1px rgba(0,0,0,0.192);}"
                   ".info-container h3{font-size:1.5em;margin:1px;}"
                   ".btn-container{display:flex;justify-content:center;margin-top:8px;}"
                   "h1{color:black;font-weight:800;}"
                   "h2{margin-top:20px;color:white;font-weight:800;text-shadow:1px 1px 1px rgba(0,0,0,0.274);}"
                   "p{font-size:1.2em;}"
                   ".elementOn{background:linear-gradient(to bottom,#b7deed 0%,#71ceef 50%,#21b4e2 51%,#b7deed 100%);border-radius:8px 0 0 8px;}"
                   ".elementOff{background:linear-gradient(to bottom,#f3c5bd 0%,#e86c57 50%,#ea2803 51%,#ff6600 75%,#c72200 100%);border-radius:0 8px 8px 0;}"
                   ".btn{flex:1;max-width:180px;border:none;padding:10px 20px;color:white;font-size:1em;cursor:pointer;transition:all 200ms;"
                   "text-shadow:1px 1px 1px rgba(0,0,0,0.692);}"
                   ".btn:hover{filter:brightness(1.1);} .btn:active{filter:brightness(0.9);}"
                   "table{margin:20px auto;border-collapse:collapse;width:80%;max-width:600px;background:rgba(255,255,255,0.1);border-radius:10px;overflow:hidden;}"
                   "th,td{padding:10px;border-bottom:1px solid rgba(255,255,255,0.275);text-shadow:1px 1px 1px rgba(0,0,0,0.338);}"
                   "th{background:linear-gradient(to bottom,#a9db80 0%,#96c56f 100%);color:white;text-shadow:1px 1px 1px rgba(0,0,0,0.692);}"
                   "</style></head><body>"));

    client.print(F("<h1>ESP32 Sensor Data</h1>"
                   "<article class='info-container'>"
                   "<h3>Datos Actuales</h3>"));
    client.print(F("<p><b>Temperatura:</b> "));
    client.print(tempC);
    client.print(F(" C°</p>"));
    client.print(F("<p><b>Gas:</b> "));
    client.print(gasValue);
    client.print(F("</p>"));
    client.print(F("<p><b>Llama:</b> "));
    client.print(flameDetected ? "SI" : "NO");
    client.print(F("</p>"));
    client.print(F("<p><b>Estado: "));
    client.print(razonAlerta);
    client.print(F("</b></p></article>"));


    client.print(F("<div class='btn-container'>"
                   "<a href='/onLCD'><button class='elementOn btn'>Encender LCD</button></a>"
                   "<a href='/offLCD'><button class='btn elementOff'>Apagar LCD</button></a></div>"));

    client.print(F("<div class='btn-container'>"
                   "<a href='/onBuzzer'><button class='elementOn btn'>Encender Buzzer</button></a>"
                   "<a href='/offBuzzer'><button class='btn elementOff'>Apagar Buzzer</button></a></div>"));

    client.print(F("<div class='btn-container'>"
                   "<a href='/onRGB'><button class='elementOn btn'>Encender RGB</button></a>"
                   "<a href='/offRGB'><button class='btn elementOff'>Apagar RGB</button></a></div>"));

    client.print(F("<h2>Últimos Registros</h2><table><tr><th>Registro</th></tr>"));
    for (int i = 0; i < MAX_REGISTROS; i++) {
      int idx = (registroIndex - 1 - i + MAX_REGISTROS) % MAX_REGISTROS;
      if (registros[idx] != "") {
        client.print(F("<tr><td>"));
        client.print(registros[idx]);
        client.print(F("</td></tr>"));
      }
    }
    client.print(F("</table></body></html>"));
    client.print("<meta http-equiv='refresh' content='1'>");
    client.stop();
  }
  delay(1000);
}