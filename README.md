# Smart Real-Time Anomaly Detection in IIOT

## Dataset Preparation for ML Model Training

### List of Attributes in the Dataset

1. Date & Time - timestamp
1. Temperature - BMP280 Sensor
1. Pressure - BMP280 Sensor
1. Angles in X, Y, Z - MPU6050
1. Acceleration X, Y, Z - MPU6050
1. Altitude - BMP280 Sensor

### Working of the Hardware Code of the Arduino

This ESP32 code implements a **sensor web server** that reads data from a **BMP280** (barometric pressure/temperature) and an **MPU6050** (accelerometer/gyroscope), serves the data via Wi-Fi, and displays summary information on an **I2C LCD**. Crucially, it includes **persistent calibration** for the MPU6050 and an **IR (Infrared) motion alert** system.

Here is a detailed explanation of its working, broken down by component and function:

---

## 🏗️ Core Components and Initialization

### 1. Libraries and Hardware Setup

The code uses several libraries to interface with the hardware and network:

* **`WiFi.h` & `WebServer.h`**: Manages the Wi-Fi connection and runs the HTTP server.
* **`Wire.h`**: Initializes the **I2C (Inter-Integrated Circuit)** bus, which is used to communicate with both the BMP280 and MPU6050 sensors, as well as the LCD module. The I2C pins are set to **SDA=21** and **SCL=22**.
* **`Adafruit_BMP280.h`**: Driver for the BMP280 sensor.
* **`MPU6050_light.h`**: Driver for the MPU6050 Inertial Measurement Unit (IMU).
* **`LiquidCrystal_I2C.h`**: Driver for the 16x2 LCD display module.
* **`Preferences.h`**: Used for the **ESP32 Non-Volatile Storage (NVS)**, which allows the code to save MPU6050 calibration offsets permanently, even after a restart.

### 2. `setup()` Function

The `setup()` function performs all initialization:

1. **Serial Communication:** Starts the serial monitor (`Serial.begin(115200)`).
2. **I2C & Sensors:** Initializes the I2C bus and attempts to start the **BMP280** and **MPU6050**. A warning is printed if a sensor is not found.
3. **Calibration Load:** Calls `loadOffsetsFromPrefs()` to check the ESP32's internal memory for previously saved MPU calibration data. If found, it uses these **offsets** for more accurate readings.
4. **LCD Init:** Initializes the LCD screen.
5. **Wi-Fi Connection:** Connects to the Wi-Fi network using the defined `WIFI_SSID` and `WIFI_PASS`.
6. **Web Server Setup:** Sets up the three main HTTP endpoints (`/`, `/data`, `/calibrate`) and starts the server on port 80.

---

## 📊 MPU6050 Calibration and Persistence

The most advanced feature is the ability to calibrate the MPU6050's gyroscope and accelerometer biases and save them persistently.

### 1. Calibration Endpoints (`/calibrate`)

* When a user clicks the "Calibrate Now" button on the web page, the browser sends a request to the **`/calibrate`** endpoint.
* The `handleCalibrate()` function executes the **`doAveragingCalibration()`** routine. This routine is **blocking** (the server pauses) for about $800 \times 3 \text{ms} = 2.4$ seconds.
* The routine takes hundreds of raw MPU samples. For the gyroscope, it averages the readings to find the **static bias** (the value when the MPU is perfectly still). For the accelerometer, it assumes the MPU is **flat and still**, so the expected readings are $A_x=0g$, $A_y=0g$, and $A_z=+1g$ (due to gravity). The difference between the measured average and the expected value becomes the offset. 

### 2. Offsets Storage

* If calibration succeeds, the new offset values (`accOffX`, `gyrOffX`, etc.) are updated in the global variables.
* The **`saveOffsetsToPrefs()`** function is called, which uses the `Preferences` library to write these offsets to the **ESP32's flash memory (NVS)**. This ensures the calibrated offsets are preserved across power cycles.

---

## 🌐 Web Server Endpoints

The ESP32 acts as a web server, serving two main resources:

### 1. Root Page (`/`) - `handleRoot()`

* Serves an **HTML page** that acts as a simple sensor dashboard.
* The HTML page uses **JavaScript** to continuously fetch data from the `/data` endpoint every 3 seconds and update the values on the screen.
* The page also includes a "Calibrate Now" button that triggers the `/calibrate` endpoint.

### 2. Data Endpoint (`/data`) - `handleData()`

* When the browser requests this endpoint, the function calls **`readSensors()`**.
* `readSensors()` gets the latest readings from BMP280 (Temp, Pressure, Altitude) and the MPU6050 (Corrected Accel and Tilt Angles, including the **Euler angles** calculated from the corrected accelerometer data).
* The function then formats this data into a **JSON string** and sends it back to the browser.

### 3. Security

* The `checkAuth()` helper implements basic **HTTP Basic Authentication** (`WWW_USER`/`WWW_PASS`) for all endpoints if `USE_BASIC_AUTH` is set to `true`.

---

## 📱 LCD and IR Alert

### 1. LCD Normal Display (`updateLCD()`)

* In the main `loop()`, the LCD is updated once every second (`LCD_UPDATE_MS = 1000`) with the latest **Temperature, Altitude, and X/Y tilt angles** from the MPU.

### 2. IR Alert System

* The state of the digital **IR_PIN (pin 27)** is read. The code assumes **LOW** means the IR sensor has detected something (e.g., motion).
* The main `loop()` checks if the current state (`irActive`) is different from the previous state (`lastIrActive`).
* If an alert is triggered, **`showIrAlert()`** is called, which overwrites the LCD with "IR Triggered / Alert !!!"
* When the IR signal clears, **`restoreNormalDisplay()`** is called to immediately switch back to showing sensor data.

### 3. `loop()` Function

The `loop()` function is where the core real-time tasks are managed:

* **`mpu.update()`:** Called very frequently (the main loop delays for only 20ms) to ensure the MPU's internal **Digital Motion Processor (DMP)** and angle fusion algorithm remain accurate.
* **`server.handleClient()`:** Essential for responding to Wi-Fi requests (HTML, JSON, Calibration).
* **IR Logic:** Checks and handles the IR sensor state change.
* **LCD Refresh:** Periodically updates the LCD display *unless* an IR alert is currently active.
