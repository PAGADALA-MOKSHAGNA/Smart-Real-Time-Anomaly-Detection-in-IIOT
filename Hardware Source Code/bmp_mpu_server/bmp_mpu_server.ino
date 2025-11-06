/*
  ESP32 BMP280 + MPU6050 Web Server 
  - Shows IP in Serial
  - Serves '/' (HTML) and '/data' (JSON) endpoints
  - Browser polls /data every 3 seconds to update UI
  - I2C: SDA=21, SCL=22 (ESP32)
  - Requires libraries: Adafruit_BMP280, MPU6050_light
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <MPU6050_light.h>
#include "LiquidCrystal_I2C.h"

// ---------- USER CONFIG ----------
const char* WIFI_SSID = "Eshu";
const char* WIFI_PASS = "rajmohan";

// Basic HTTP auth (optional). Set USE_BASIC_AUTH=false to disable.
const bool USE_BASIC_AUTH = true;
const char* WWW_USER = "Mokshagna";
const char* WWW_PASS = "MLG333";
// ---------------------------------

#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_FREQ 100000UL
#define BMP_I2C_ADDR 0x76
#define SEALEVELPRESSURE_HPA 1013.25F
#define SERIAL_BAUD 115200
#define POLL_INTERVAL_MS 1000UL  // 1 seconds polling from client - server returns instant readings

Adafruit_BMP280 bmp;
MPU6050 mpu(Wire);
WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);

bool bmp_ok = false;

struct SensorReading {
  unsigned long esp_ms;
  float temperature;
  float pressure;
  float altitude;
  float angleX, angleY, angleZ;
  float accX, accY, accZ;
};

SensorReading readSensors() {
  SensorReading r;
  r.esp_ms = millis();

  // BMP readings (if available)
  if (bmp_ok) {
    r.temperature = bmp.readTemperature();            // °C
    r.pressure    = bmp.readPressure() / 100.0F;      // hPa
    r.altitude    = bmp.readAltitude(SEALEVELPRESSURE_HPA); // m
  } else {
    r.temperature = 0.0;
    r.pressure = 0.0;
    r.altitude = 0.0;
  }

  // MPU readings (ensure update called frequently in loop)
  r.angleX = mpu.getAngleX();
  r.angleY = mpu.getAngleY();
  r.angleZ = mpu.getAngleZ();
  r.accX = mpu.getAccX();
  r.accY = mpu.getAccY();
  r.accZ = mpu.getAccZ();

  return r;
}

// ---------- Basic Auth helper ----------
bool checkAuth() {
  if (!USE_BASIC_AUTH) return true;
  if (!server.hasHeader("Authorization")) return false;
  return server.authenticate(WWW_USER, WWW_PASS);
}

// ---------- HTTP handlers ----------
void handleRoot() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }

  const char* html = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32 Sensor Dashboard</title>
  <style>
    body{font-family:Arial;margin:20px}
    .row{margin:8px 0}
    .label{display:inline-block;width:160px;font-weight:600}
    .value{display:inline-block}
  </style>
</head>
<body>
  <h2>ESP32 Sensor Dashboard</h2>
  <div class="row"><span class="label">Device IP:</span><span id="devip">...</span></div>
  <div class="row"><span class="label">Temperature (°C):</span><span id="temp">...</span></div>
  <div class="row"><span class="label">Pressure (hPa):</span><span id="pres">...</span></div>
  <div class="row"><span class="label">Altitude (m):</span><span id="alt">...</span></div>
  <div class="row"><span class="label">Angle X,Y,Z (°):</span><span id="ang">...</span></div>
  <div class="row"><span class="label">Acc X,Y,Z (g):</span><span id="acc">...</span></div>
  <div class="row"><span class="label">Copy for ML Model:</span>
  <span id="csvline" style="font-family: monospace; color:#006400">...</span>
  </div>
<script>
function update() {
  fetch('/data').then(r => {
    if (!r.ok) throw 'fetch error ' + r.status;
    return r.json();
  }).then(j => {
    document.getElementById('devip').textContent = window.location.host;
    document.getElementById('temp').textContent = j.Temperature_C.toFixed(2);
    document.getElementById('pres').textContent = j.Pressure_hPa.toFixed(2);
    document.getElementById('alt').textContent = j.Altitude_m.toFixed(2);
    document.getElementById('ang').textContent = j.AngleX.toFixed(2) + ', ' + j.AngleY.toFixed(2) + ', ' + j.AngleZ.toFixed(2);
    document.getElementById('acc').textContent = j.AccX_g.toFixed(3) + ', ' + j.AccY_g.toFixed(3) + ', ' + j.AccZ_g.toFixed(3);

    // Create CSV-style string in model order:
    // Temperature_C,Pressure_hPa,AngleX,AngleY,AngleZ,AccX_g,AccY_g,AccZ_g,Altitude_m
    const csvline = [
      j.Temperature_C.toFixed(2),
      j.Pressure_hPa.toFixed(2),
      j.AngleX.toFixed(2),
      j.AngleY.toFixed(2),
      j.AngleZ.toFixed(2),
      j.AccX_g.toFixed(3),
      j.AccY_g.toFixed(3),
      j.AccZ_g.toFixed(3),
      j.Altitude_m.toFixed(2)
    ].join(',');

    document.getElementById('csvline').textContent = csvline;

  }).catch(e => {
    console.error('Update error:', e);
  });
}

// initial update and then poll every 3s
update();
setInterval(update, 3000);
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }

  // return current sensor values as JSON
  SensorReading r = readSensors();
  String json = "{";
  json += "\"ESP_ms\":" + String(r.esp_ms) + ",";
  json += "\"Temperature_C\":" + String(r.temperature, 2) + ",";
  json += "\"Pressure_hPa\":"   + String(r.pressure, 2) + ",";
  json += "\"Altitude_m\":"     + String(r.altitude, 2) + ",";
  json += "\"AngleX\":" + String(r.angleX, 2) + ",";
  json += "\"AngleY\":" + String(r.angleY, 2) + ",";
  json += "\"AngleZ\":" + String(r.angleZ, 2) + ",";
  json += "\"AccX_g\":" + String(r.accX, 3) + ",";
  json += "\"AccY_g\":" + String(r.accY, 3) + ",";
  json += "\"AccZ_g\":" + String(r.accZ, 3);
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { delay(10); }
  delay(50);

  // Display Setup - LCD - 1602A
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Display Started...");
  delay(1500);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready...");


  Serial.println();
  Serial.println("ESP32 BMP280 + MPU6050 Web Server");

  // Setup I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ);

  // Initialize BMP280 once (guard with bmp_ok)
  bmp_ok = bmp.begin(BMP_I2C_ADDR);
  if (!bmp_ok) {
    Serial.printf("Warning: BMP280 not found at 0x%02X\n", BMP_I2C_ADDR);
  } else {
    Serial.println("BMP280 initialized.");
  }

  // Initialize MPU6050
  byte status = mpu.begin();
  if (status == 0) {
    Serial.println("MPU6050 initialized.");
  } else {
    Serial.printf("Warning: MPU6050 init returned status %u\n", status);
  }

  Serial.println("Calculating MPU offsets (keep module still)...");
  delay(200);
  mpu.calcOffsets();
  Serial.println("MPU offsets calculated.");

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed. Check SSID/password and network.");
  }

  // HTTP routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  // keep MPU integration accurate: call update frequently
  mpu.update();

  // handle HTTP clients (page requests)
  server.handleClient();

  delay(5); // small yield
}
