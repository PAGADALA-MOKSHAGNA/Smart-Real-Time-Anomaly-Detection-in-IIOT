/*
  MPU6050 simple vibration + orientation + temperature logger
  ESP32 + MPU6050_light
*/

#include <Wire.h>
#include <MPU6050_light.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define I2C_FREQ 100000UL
#define SERIAL_BAUD 115200

MPU6050 mpu(Wire);
unsigned long printTimer = 0;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(100);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_FREQ);

    Serial.println("\nESP32 MPU6050 - Accel + Angle + Temp");

    // Init MPU
    byte status = mpu.begin();
    if (status == 0)
    {
        Serial.println("MPU initialized.");
    }
    else
    {
        Serial.print("MPU init failed, status=");
        Serial.println(status);
    }

    // Offsets for better stability (keep device still)
    delay(200);
    Serial.println("Calculating offsets...");
    mpu.calcOffsets();
    Serial.println("Offsets set.\n");

    printTimer = millis();
}

void loop()
{
    mpu.update(); // update sensor

    if ((millis() - printTimer) > 3000)
    {
        // Raw accelerometer values (in g)
        Serial.print("Acc (g) -> X: ");
        Serial.print(mpu.getAccX(), 3);
        Serial.print("\tY: ");
        Serial.print(mpu.getAccY(), 3);
        Serial.print("\tZ: ");
        Serial.print(mpu.getAccZ(), 3);

        // Orientation (calculated angles)
        Serial.print(" | Angles -> X: ");
        Serial.print(mpu.getAngleX(), 2);
        Serial.print("\tY: ");
        Serial.print(mpu.getAngleY(), 2);
        Serial.print("\tZ: ");
        Serial.print(mpu.getAngleZ(), 2);

        // Temperature (°C)
        Serial.print(" | Temp: ");
        Serial.print(mpu.getTemp(), 2);
        Serial.println(" °C");

        printTimer = millis();
    }

    delay(5);
}