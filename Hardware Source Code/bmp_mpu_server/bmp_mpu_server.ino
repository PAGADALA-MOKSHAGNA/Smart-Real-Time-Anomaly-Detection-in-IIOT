/*
  ESP32 BMP280 + MPU6050 Web Server (with persistent calibration + LCD + IR alert)
  - Shows IP in Serial
  - Serves '/' (HTML) and '/data' (JSON) endpoints
  - New endpoint '/calibrate' performs averaging calibration and saves offsets to Preferences
  - I2C: SDA=21, SCL=22 (ESP32)
  - Requires libraries: Adafruit_BMP280, MPU6050_light, LiquidCrystal_I2C
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <MPU6050_light.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>

// ---------- USER CONFIG ----------
const char* WIFI_SSID = "Janardhana Rao";
const char* WIFI_PASS = "Madhavi#888";
const int IR_PIN = 27;

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
#define POLL_INTERVAL_MS 1000UL  // not used for server polling but kept

Adafruit_BMP280 bmp;
MPU6050 mpu(Wire);
WebServer server(80);
Preferences prefs;

bool bmp_ok = false;

LiquidCrystal_I2C lcd(0x27, 16, 2); // change 0x27 to 0x3F if your module reports that address
unsigned long lcd_last = 0;
const unsigned long LCD_UPDATE_MS = 1000; // update LCD once per second when not alert

// ---------- Calibration offsets (default values from your calibration) ----------
float accOffX = 0.130075f;
float accOffY = -0.001063f;
float accOffZ = -0.053148f;

float gyrOffX = 3.4171f;
float gyrOffY = 3.6740f;
float gyrOffZ = 3.6400f;

bool offsetsLoaded = false;

struct SensorReading {
  unsigned long esp_ms;
  float temperature;
  float pressure;
  float altitude;
  float angleX, angleY, angleZ;
  float accX, accY, accZ;
};

static inline float rad2deg(float r) { return r * 57.29577951308232f; }

// ---------- Helpers: save/load offsets ----------
void saveOffsetsToPrefs() {
  prefs.begin("mpu", false);
  prefs.putFloat("ax", accOffX);
  prefs.putFloat("ay", accOffY);
  prefs.putFloat("az", accOffZ);
  prefs.putFloat("gx", gyrOffX);
  prefs.putFloat("gy", gyrOffY);
  prefs.putFloat("gz", gyrOffZ);
  prefs.putBool("ok", true);
  prefs.end();
}

void loadOffsetsFromPrefs() {
  prefs.begin("mpu", true);
  if (prefs.getBool("ok", false)) {
    accOffX = prefs.getFloat("ax", accOffX);
    accOffY = prefs.getFloat("ay", accOffY);
    accOffZ = prefs.getFloat("az", accOffZ);
    gyrOffX = prefs.getFloat("gx", gyrOffX);
    gyrOffY = prefs.getFloat("gy", gyrOffY);
    gyrOffZ = prefs.getFloat("gz", gyrOffZ);
    offsetsLoaded = true;
  }
  prefs.end();
}

// ---------- Calibration routine (averaging) ----------
bool doAveragingCalibration(unsigned int samples, unsigned long sampleDelayMs,
                            float &out_accOffX, float &out_accOffY, float &out_accOffZ,
                            float &out_gyrOffX, float &out_gyrOffY, float &out_gyrOffZ) {
  double sAx = 0, sAy = 0, sAz = 0;
  double sGx = 0, sGy = 0, sGz = 0;

  delay(200); // small settle

  for (unsigned int i = 0; i < samples; ++i) {
    mpu.update();
    // library returns accel in g and gyro in deg/s
    float ax = mpu.getAccX();
    float ay = mpu.getAccY();
    float az = mpu.getAccZ();
    float gx = mpu.getGyroX();
    float gy = mpu.getGyroY();
    float gz = mpu.getGyroZ();

    sAx += ax;
    sAy += ay;
    sAz += az;
    sGx += gx;
    sGy += gy;
    sGz += gz;

    delay(sampleDelayMs);
  }

  float meanAx = (float)(sAx / samples);
  float meanAy = (float)(sAy / samples);
  float meanAz = (float)(sAz / samples);

  float meanGx = (float)(sGx / samples);
  float meanGy = (float)(sGy / samples);
  float meanGz = (float)(sGz / samples);

  // Desired: ax=0, ay=0, az=+1g when flat
  out_accOffX = meanAx - 0.0f;
  out_accOffY = meanAy - 0.0f;
  out_accOffZ = meanAz - 1.0f;

  out_gyrOffX = meanGx;
  out_gyrOffY = meanGy;
  out_gyrOffZ = meanGz;

  // sanity check: ensure meanAz reasonably near ±1g else fail
  if (fabs(meanAz) < 0.5f) {
    // probably sensor not oriented or something wrong
    return false;
  }
  return true;
}

// ---------- Sensor read (applies offsets and computes corrected tilt) ----------
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

  // Ensure MPU values are fresh
  mpu.update();

  // Get raw accel (library gives g)
  float rawAx = mpu.getAccX();
  float rawAy = mpu.getAccY();
  float rawAz = mpu.getAccZ();

  // Apply stored offsets (subtract bias)
  float corrAx = rawAx - accOffX;
  float corrAy = rawAy - accOffY;
  float corrAz = rawAz - accOffZ;

  // Compute tilt X and Y (degrees) from corrected accel
  float angleX = atan2(corrAy, corrAz) * 180.0f / PI; // pitch-like
  float angleY = atan2(-corrAx, sqrtf(corrAy * corrAy + corrAz * corrAz)) * 180.0f / PI; // roll-like

  // For Z (yaw) we will use library fused yaw (may drift) - optional improvements possible
  float angleZ = mpu.getAngleZ();

  r.angleX = angleX;
  r.angleY = angleY;
  r.angleZ = angleZ;
  r.accX = corrAx;
  r.accY = corrAy;
  r.accZ = corrAz;

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
  <div class="row">
    <button onclick="calibrate()">Calibrate Now</button>
    <span id="calmsg"></span>
  </div>
    <div class="row">
    <span class="label">Model:</span>
    <span id="model_result">...</span>
  </div>
<script>
const FLASK_PREDICT_URL = "http://192.168.31.78:5000/predict";
async function update() {
  try {
    // 1) Fetch ESP32 raw sensor data (keep existing behavior)
    const r = await fetch('/data');
    if (!r.ok) throw 'fetch error ' + r.status;
    const j = await r.json();

    // update raw sensor fields (same as before)
    document.getElementById('devip').textContent = window.location.host;
    document.getElementById('temp').textContent = j.Temperature_C.toFixed(2);
    document.getElementById('pres').textContent = j.Pressure_hPa.toFixed(2);
    document.getElementById('alt').textContent = j.Altitude_m.toFixed(2);
    document.getElementById('ang').textContent = j.AngleX.toFixed(2) + ', ' + j.AngleY.toFixed(2) + ', ' + j.AngleZ.toFixed(2);
    document.getElementById('acc').textContent = j.AccX_g.toFixed(3) + ', ' + j.AccY_g.toFixed(3) + ', ' + j.AccZ_g.toFixed(3);

    // Create CSV-style string in model order:
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

    // 2) Fetch model prediction from your Flask middleware
    //    (do this after updating the raw values so UI remains responsive)
    try {
      const resp = await fetch(FLASK_PREDICT_URL, { method: "GET", mode: "cors" });
      if (!resp.ok) {
        console.error("Flask /predict error:", resp.status, resp.statusText);
        document.getElementById('model_result').textContent = "Err";
      } else {
        const pdata = await resp.json();
        const output = (pdata && pdata.model_output !== undefined) ? pdata.model_output : null;
        document.getElementById('model_result').textContent = (output === null) ? "No output" : String(output);
      }
    } catch (pferr) {
      console.error("Failed to fetch prediction:", pferr);
      document.getElementById('model_result').textContent = "Conn Err";
    }

  } catch (e) {
    console.error('Update error:', e);
  }
}


function calibrate() {
  if (!confirm('Keep MPU perfectly still and flat. Click OK to start calibration (takes ~3s).')) return;
  document.getElementById('calmsg').textContent = ' Calibrating...';
  fetch('/calibrate').then(r => r.json()).then(j => {
    document.getElementById('calmsg').textContent = ' Done. Offsets saved.';
    setTimeout(()=>document.getElementById('calmsg').textContent = '', 3000);
    console.log('Calibration result', j);
  }).catch(e => {
    document.getElementById('calmsg').textContent = ' Calibration failed.';
    console.error(e);
    setTimeout(()=>document.getElementById('calmsg').textContent = '', 3000);
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

// /calibrate endpoint: runs averaging calibration, saves offsets, returns JSON
void handleCalibrate() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }

  // perform averaging calibration (blocking) - choose samples and delay
  const unsigned int samples = 800;
  const unsigned long sampDelayMs = 3;

  Serial.println("Calibration: keep MPU perfectly still and flat.");
  bool ok = false;
  float new_ax_off=0, new_ay_off=0, new_az_off=0;
  float new_gx_off=0, new_gy_off=0, new_gz_off=0;

  ok = doAveragingCalibration(samples, sampDelayMs,
                              new_ax_off, new_ay_off, new_az_off,
                              new_gx_off, new_gy_off, new_gz_off);

  String json;
  if (!ok) {
    Serial.println("Calibration failed (sensor not stable or orientation unexpected).");
    json = "{\"ok\":false}";
    server.send(200, "application/json", json);
    return;
  }

  // apply new offsets to globals and persist
  accOffX = new_ax_off;
  accOffY = new_ay_off;
  accOffZ = new_az_off;
  gyrOffX = new_gx_off;
  gyrOffY = new_gy_off;
  gyrOffZ = new_gz_off;

  saveOffsetsToPrefs();

  json = "{";
  json += "\"ok\":true,";
  json += "\"accOffX\":" + String(accOffX,6) + ",";
  json += "\"accOffY\":" + String(accOffY,6) + ",";
  json += "\"accOffZ\":" + String(accOffZ,6) + ",";
  json += "\"gyrOffX\":" + String(gyrOffX,4) + ",";
  json += "\"gyrOffY\":" + String(gyrOffY,4) + ",";
  json += "\"gyrOffZ\":" + String(gyrOffZ,4);
  json += "}";
  server.send(200, "application/json", json);
}

// -------------------- LCD / IR alert helpers --------------------
bool lastIrActive = false;    // last known state written to LCD
bool irActive = false;        // current state

// show IR alert on LCD (writes once; no clear)
void showIrAlert() {
  const char *l0 = "IR Triggered";
  const char *l1 = "Alert !!!";
  char buf[17];

  // line 0
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "%-16s", l0); // pad right to 16
  lcd.print(buf);

  // line 1
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "%-16s", l1);
  lcd.print(buf);
}

// Restore normal sensor display by calling updateLCD once (overwrites lines)
void restoreNormalDisplay() {
  SensorReading s = readSensors();
  // call updateLCD to draw sensor values (it will pad/overwrite entire lines)
  // Use a private version that doesn't update lcd_last so immediate draw occurs
  // We'll write directly here similar to updateLCD.
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "T:%4.1fC A:%5.1fm", s.temperature, s.altitude);
  snprintf(line2, sizeof(line2), "X:%5.1f Y:%5.1f", s.angleX, s.angleY);

  lcd.setCursor(0, 0);
  lcd.print(line1);
  int len1 = strlen(line1);
  for (int i = len1; i < 16; ++i) lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print(line2);
  int len2 = strlen(line2);
  for (int i = len2; i < 16; ++i) lcd.print(' ');
}

// existing updateLCD used by periodic refresh (keeps lcd_last timing)
void updateLCD(const SensorReading &s) {
  // We'll display: line1 => Temp(°C) and Alt(m) (short), line2 => AngX,AngY
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "T:%4.1fC A:%5.1fm", s.temperature, s.altitude);
  snprintf(line2, sizeof(line2), "X:%5.1f Y:%5.1f", s.angleX, s.angleY);

  lcd.setCursor(0,0);
  lcd.print(line1);
  int len1 = strlen(line1);
  for (int i = len1; i < 16; ++i) lcd.print(' ');

  lcd.setCursor(0,1);
  lcd.print(line2);
  int len2 = strlen(line2);
  for (int i = len2; i < 16; ++i) lcd.print(' ');
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { delay(10); }
  delay(50);
  pinMode(IR_PIN, INPUT);

  Serial.println();
  Serial.println("ESP32 BMP280 + MPU6050 Web Server (with calibration + LCD + IR)");

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

  // Load any previously saved offsets
  loadOffsetsFromPrefs();
  if (offsetsLoaded) {
    Serial.println("Loaded stored MPU offsets from Preferences.");
    Serial.printf("accOffs g: %.6f, %.6f, %.6f\n", accOffX, accOffY, accOffZ);
    Serial.printf("gyrOffs dps: %.4f, %.4f, %.4f\n", gyrOffX, gyrOffY, gyrOffZ);
  } else {
    Serial.println("No stored offsets found — using built-in defaults.");
  }

  // initialize the lcd AFTER calibration/loading offsets
  lcd.init();
  lcd.backlight();
  delay(50);

  // show a boot message briefly (no long blocking delays)
  lcd.setCursor(0,0);
  lcd.print("ESP32 Sensors");
  lcd.setCursor(0,1);
  lcd.print("Starting...");
  delay(700);
  lcd.clear();

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
  server.on("/calibrate", handleCalibrate);
  server.begin();
  Serial.println("HTTP server started.");

  // initialize lastIrActive to current state so we don't flicker at boot
  lastIrActive = digitalRead(IR_PIN) == LOW;
  if (lastIrActive) {
    showIrAlert();
  } else {
    // draw initial sensor values immediately
    restoreNormalDisplay();
  }
}

void loop() {
  // keep MPU integration accurate: call update frequently
  mpu.update();

  // Read IR pin and update LCD only on transitions
  irActive = (digitalRead(IR_PIN) == LOW); // LOW means detected per your wiring
  if (irActive != lastIrActive) {
    // state changed
    if (irActive) {
      // show alert once
      showIrAlert();
      Serial.println("IR Detected - LCD showing alert");
    } else {
      // restore normal sensor display immediately
      restoreNormalDisplay();
      Serial.println("IR cleared - LCD restored");
    }
    lastIrActive = irActive;
  }

  // handle HTTP clients (page requests)
  server.handleClient();

  // Periodic LCD refresh when not in IR alert
  if (!irActive && (millis() - lcd_last > LCD_UPDATE_MS)) {
    SensorReading s = readSensors(); // readSensors already updates mpu and applies offsets
    updateLCD(s);
    lcd_last = millis();
  }

  delay(20); // shorter yield so mpu.update() is called often (improves fusion)
}