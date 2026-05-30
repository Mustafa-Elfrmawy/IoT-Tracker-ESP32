# 📍 IoT Machine GPS Tracker (ESP32 Client)

An ESP32-based IoT client designed for tracking any machine or vehicle. It handles real-time location data, machine status (Ignition/ACC), and communicates with a RESTful backend API via GPRS. It also supports remote execution commands (e.g., Engine Kill Switch).

---

## 🛠️ Hardware Components
This project is built using the following core components:
* **Microcontroller:** ESP32 (Handles dual-core processing & FreeRTOS tasks).
* **GPS Module:** U-blox NEO-M8N (For highly accurate positioning).
* **Network Module:** SIM800C (Provides 2G/GPRS connectivity to send HTTP requests).
* **Optocoupler:** 4N35 (Used to safely read the machine's Ignition/ACC status without frying the ESP32).
* **Relay:** 1-Channel 5V Relay (Acts as a remote kill switch to cut machine power if commanded by the server).

---

## 💻 Software & Development Environment
To run or modify this project, it is highly recommended to use:
* **Editor:** [VS Code (Visual Studio Code)](https://code.visualstudio.com/)
* **Extension:** **PlatformIO IDE** (Essential for managing library dependencies like `TinyGPS++` and `ArduinoJson`, and compiling the C++ code for ESP32).

---

## 🔌 Pin Configuration (Wiring)
Before flashing the code, ensure your hardware is wired according to these pin definitions configured in `main.cpp`:

```cpp
#define GPS_RX_PIN 19        // Connect to NEO-M8N TX
#define GPS_TX_PIN 18        // Connect to NEO-M8N RX
#define SIM_RX_PIN 16        // Connect to SIM800C TX
#define SIM_TX_PIN 17        // Connect to SIM800C RX
#define SIM800C_DTR_PIN 5    // Used to control SIM800C sleep state
#define IGNITION_PIN 4       // Connect to 4N35 Output (ACC Status)
#define RELAY_PIN 22         // Connect to the Relay Signal pin


📡 API Payload Explanation

The ESP32 sends a GET request to the backend server every 10 seconds. Here is the structure of the URL sent to the server:

http://[YOUR-SERVER-IP]/api/test?lat=...&lng=...&gia=...&giv=...&alt=...&spd=...&sig=...&icc=...

What do these parameters mean?

    lat: Latitude (The exact North/South position).

    lng: Longitude (The exact East/West position).

    gia: GPS Is Alive (Boolean 1 or 0). Indicates if the physical GPS module is responding to the ESP32.

    giv: GPS Is Valid (Boolean 1 or 0). Indicates if the GPS has successfully locked onto satellites and the coordinates are accurate.

    alt: Altitude (Height above sea level in meters).

    spd: Speed (Current speed in km/h).

    sig: Signal Strength (SIM800C network signal quality, usually between 0-31).

    icc: Ignition Contact Closed (Boolean 1 or 0). Indicates if the machine/vehicle is currently turned ON (1) or OFF (0).

✨ Key Code Features & Architecture

This code isn't just a simple loop; it's designed for harsh edge-device environments:
1. Multi-Threading with FreeRTOS & Mutex 🛡️

The ESP32 has two cores. The code utilizes xSemaphoreCreateMutex() to protect the TinyGPS++ object.

    Core 1: Constantly reads raw NMEA sentences from the GPS.

    Core 0: Handles the heavy lifting of sending HTTP requests via the SIM800C.

    The Mutex ensures that Core 0 doesn't try to read coordinates exactly while Core 1 is updating them, preventing memory crashes.

2. Auto-Recovery System 🔄

Machines lose network coverage frequently. SimService.h includes a checkHttpFailures() logic. If the HTTP request fails 3 consecutive times, the ESP32 automatically performs a hard reset to re-initialize the network modules, ensuring the device never gets stuck offline.

3. Deep Sleep & Power Saving 🔋
To prevent draining the machine's battery when parked, the system monitors the IGNITION_PIN. When the machine is turned OFF, sleepAllDevices() is triggered. It puts the SIM800C and NEO-M8N into low-power states, holds the Relay pin state using gpio_hold_en(), and puts the ESP32 into Deep Sleep until the ignition is turned back on.


4. Remote Execution via JSON ⚙️
After sending the telemetry data, the ESP32 parses the server's HTTP JSON response using ArduinoJson. If the server sends {"exec_command": "machine close"}, the ESP32 immediately triggers the Relay to cut power to the machine safely.


