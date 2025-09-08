/*
  ESP32 BMP280 + MPU6050 Serial CSV Logger (fixed)
  - I2C: SDA=21, SCL=22
  - Sets Wire clock once in setup()
  - Initializes BMP280 only once; uses bmp_ok flag to guard reads
  - Minimizes blocking in loop so MPU integration/filter is stable
  - Optional diagnostics: enable DEBUG_PRINT_GYRO to see raw gyro bias
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <MPU6050_light.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_FREQ 400000UL         // try 400kHz; switch to 100000UL if device doesn't support
#define BMP_I2C_ADDR 0x76        // change to 0x77 if needed
#define SEALEVELPRESSURE_HPA 1013.25F
#define SERIAL_BAUD 115200
#define SAMPLE_INTERVAL_MS 1000UL   // sample period (ms)

Adafruit_BMP280 bmp; // BMP280 (no humidity)
MPU6050 mpu(Wire);

unsigned long lastSample = 0;
bool headerPrinted = false;
bool bmp_ok = false;

// Uncomment to print raw gyro values for diagnostics
// #define DEBUG_PRINT_GYRO

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { delay(10); }
  delay(100);

  // Initialize I2C and set clock BEFORE device init
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ);

  Serial.println();
  Serial.println("=== ESP32 BMP280 + MPU6050 Serial CSV Logger (fixed) ===");
  Serial.printf("I2C SDA=%d SCL=%d | I2C clock=%lu Hz | Sample interval = %lu ms\n",
                SDA_PIN, SCL_PIN, I2C_FREQ, SAMPLE_INTERVAL_MS);

  // Initialize BMP280 ONCE
  bmp_ok = bmp.begin(BMP_I2C_ADDR);
  if (!bmp_ok) {
    Serial.printf("Warning: BMP280 not found at 0x%02X. Temperature/pressure/altitude will be 0.\n", BMP_I2C_ADDR);
  } else {
    Serial.println("BMP280 initialized.");
  }

  // Initialize MPU6050
  byte status = mpu.begin();
  if (status == 0) {
    Serial.println("MPU6050 initialized.");
  } else {
    Serial.printf("Warning: MPU6050 init returned status %u. Angles/accel may be invalid.\n", status);
  }

  // Calculate offsets for MPU. Increase samples for better stability.
  // mpu.calcOffsets(samples, autocalibrate) — many builds support these args
  // If your library version doesn't accept args, it will use defaults.
  delay(200);
  Serial.println("Calculating MPU offsets (keep device still)...");
  #ifdef MPU6050_LIGHT_H
    // try to call with more samples if available (safe fallback)
    // typical signature: calcOffsets(int loops = 1000, bool readCalibrate = true)
    mpu.calcOffsets(); // default; if you have overloads, you can use mpu.calcOffsets(1000, true);
  #else
    mpu.calcOffsets();
  #endif
  Serial.println("Offsets set. Starting logging...");

  lastSample = millis();
}

void printHeaderIfNeeded() {
  if (!headerPrinted) {
    Serial.println("ESP_CSV_HEADER:ESP_ms,Temperature_C,Pressure_hPa,AngleX,AngleY,AngleZ,AccX_g,AccY_g,AccZ_g,Altitude_m");
    headerPrinted = true;
  }
}

void loop() {
  unsigned long now = millis();

  // Keep update cycles frequent and predictable to avoid integration bias.
  // We call mpu.update() every loop to let it integrate with good dt values.
  mpu.update();

  // Do sampling at SAMPLE_INTERVAL_MS for BMP readings + CSV output
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;

    printHeaderIfNeeded();

    // Read BMP values (no re-init). If bmp_ok is false we'll leave values zero.
    float temperature = 0.0f;
    float pressure = 0.0f;
    float altitude = 0.0f;
    if (bmp_ok) {
      // These are relatively quick reads; avoid heavy processing here.
      temperature = bmp.readTemperature();                 // °C
      pressure = bmp.readPressure() / 100.0F;              // Pa -> hPa
      altitude = bmp.readAltitude(SEALEVELPRESSURE_HPA);   // m
    }

    // Read MPU values AFTER mpu.update()
    float angleX = mpu.getAngleX();
    float angleY = mpu.getAngleY();
    float angleZ = mpu.getAngleZ();
    float accX = mpu.getAccX();
    float accY = mpu.getAccY();
    float accZ = mpu.getAccZ();

    // Optional diagnostic print of raw gyro bias (uncomment define above)
    #ifdef DEBUG_PRINT_GYRO
      Serial.printf("Gyro (raw) X: %.4f Y: %.4f Z: %.4f\n", mpu.getGyroX(), mpu.getGyroY(), mpu.getGyroZ());
    #endif

    // CSV output (ESP_ms,Temperature_C,Pressure_hPa,AngleX,AngleY,AngleZ,AccX_g,AccY_g,AccZ_g,Altitude_m)
    Serial.print(now); Serial.print(',');
    Serial.print(temperature, 2); Serial.print(',');
    Serial.print(pressure, 2); Serial.print(',');
    Serial.print(angleX, 2); Serial.print(',');
    Serial.print(angleY, 2); Serial.print(',');
    Serial.print(angleZ, 2); Serial.print(',');
    Serial.print(accX, 3); Serial.print(',');
    Serial.print(accY, 3); Serial.print(',');
    Serial.print(accZ, 3); Serial.print(',');
    Serial.println(altitude, 2);
  }

  // Small short sleep to yield; keep it tiny so mpu.update() is called frequently
  delay(5);
}