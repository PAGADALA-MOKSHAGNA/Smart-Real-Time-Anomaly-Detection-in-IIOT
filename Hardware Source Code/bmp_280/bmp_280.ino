/*
  Minimal BMP280 Serial logger for ESP32
  - Prints temperature (°C), pressure (hPa) and altitude (m) to Serial Monitor
  - Uses I2C on ESP32 pins SDA=21, SCL=22 (same bus as MPU6050)
  - Uses Adafruit_BMP280 library
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_ADDR 0x76            // change to 0x77 if your module uses that
#define SEALEVELPRESSURE_HPA 1013.25F
#define SERIAL_BAUD 115200
#define READ_INTERVAL_MS 1000UL  // print every 1 second

Adafruit_BMP280 bmp; // I2C interface

unsigned long lastRead = 0;

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { delay(10); } // wait for Serial
  delay(100);

  // Initialize I2C explicitly for ESP32
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println();
  Serial.println("=== ESP32 BMP280 Serial Logger ===");
  Serial.print("I2C pins SDA=");
  Serial.print(SDA_PIN);
  Serial.print(" SCL=");
  Serial.println(SCL_PIN);

  // Try to initialize sensor
  if (!bmp.begin(I2C_ADDR)) {
    Serial.print("BMP280 not found at 0x");
    Serial.print(I2C_ADDR, HEX);
    Serial.println(". Continuing — sensor reads will be zero.");
  } else {
    Serial.println("BMP280 initialized successfully.");
  }

  Serial.println("-------------------------------------------");
  Serial.println("Timestamp(ms)\tTemp(°C)\tPressure(hPa)\tAltitude(m)");
  Serial.println("-------------------------------------------");
}

void loop() {
  unsigned long now = millis();
  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;

    float temperature = 0.0;
    float pressure    = 0.0;
    float altitude    = 0.0;

    if (bmp.begin(I2C_ADDR)) {
      temperature = bmp.readTemperature();           // °C
      pressure    = bmp.readPressure() / 100.0F;     // Pa -> hPa
      altitude    = bmp.readAltitude(SEALEVELPRESSURE_HPA); // m
    }

    // Print tab-separated values for easy CSV parsing
    Serial.print(now);
    Serial.print('\t');
    Serial.print(temperature, 2); Serial.print('\t');
    Serial.print(pressure, 2);    Serial.print('\t');
    Serial.println(altitude, 2);
  }

  delay(5);
}
