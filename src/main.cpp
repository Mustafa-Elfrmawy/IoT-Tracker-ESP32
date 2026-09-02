#include <Arduino.h>
#include <TinyGPS++.h>
#include "SimService.h"
#include "GpsService.h"
#include "Sensors.h" // Added for Open/Closed Principle
#include <ArduinoJson.h>
#include <esp_sleep.h>

#define DEVICE_ID "ESP_TRACKER_001" // Dummy Device ID
#define SERVER_IP "161.97.84.206"   // UDP Server IP
#define SERVER_PORT "8070"          // UDP Server Port
#define TEST_SERVER_IP "147.185.221.213"
#define TEST_SERVER_PORT "23686"
#define TEST true

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
SemaphoreHandle_t gpsMutex;

RTC_DATA_ATTR bool isMachineKilled = false;

bool gps_is_alive = false;
bool is_wakeup_ping = false;
bool is_timer_wakeup = false;

#define GPS_RX_PIN 19
#define GPS_TX_PIN 18
#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define IGNITION_PIN 22
#define RELAY_PIN 4
#define CONTROL_PIN 2

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 180

TaskHandle_t ServerTask;

// Initialize Ignition Sensor
IgnitionSensor ignitionSensor(IGNITION_PIN);

void goToDeepSleep()
{
  Serial.println("\n[SLEEP] Entering Deep Sleep for 20 minutes or until ACC ON...");
  Serial.flush();

  digitalWrite(CONTROL_PIN, LOW);

  pinMode(GPS_RX_PIN, OUTPUT);
  digitalWrite(GPS_RX_PIN, LOW);

  pinMode(GPS_TX_PIN, OUTPUT);
  digitalWrite(GPS_TX_PIN, LOW);

  pinMode(SIM_RX_PIN, OUTPUT);
  digitalWrite(SIM_RX_PIN, LOW);

  pinMode(SIM_TX_PIN, OUTPUT);
  digitalWrite(SIM_TX_PIN, LOW);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)IGNITION_PIN, 0);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  esp_deep_sleep_start();
}

void initSerial2AndGPS()
{
  Serial2.begin(9600, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  pinMode(RELAY_PIN, OUTPUT);

  // Setup Sensor Pin
  ignitionSensor.init();

  if (isMachineKilled)
  {
    digitalWrite(RELAY_PIN, LOW);
  }
  else
  {
    digitalWrite(RELAY_PIN, HIGH);
  }
}

void checkSim800cStatus()
{
  Serial.println("-> Starting SIM Health Check...");
  for (uint8_t i = 0; i < 50; i++)
  {
    if (SimService::isAlive())
    {
      Serial.println("-> SIM Module is Ready!");
      return;
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  Serial.println("-> SIM Module Failed. Restarting ESP...");
  ESP.restart();
}

bool checkGpsNeoStatus()
{
  Serial.println("-> Performing Deep GPS Check...");
  unsigned long start = millis();
  uint32_t initialChars = gps.charsProcessed();

  while (millis() - start < 5000)
  {
    while (gpsSerial.available() > 0)
    {
      char c = gpsSerial.read();
      if (xSemaphoreTake(gpsMutex, portMAX_DELAY) == pdTRUE)
      {
        bool encoded = gps.encode(c);
        xSemaphoreGive(gpsMutex);
        if (encoded)
        {
          Serial.println("-> [SUCCESS] GPS is alive.");
          return true;
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  if (gps.charsProcessed() - initialChars < 10)
  {
    Serial.println("-> [WARNING] No valid GPS data.");
    return false;
  }
  Serial.println("-> [INFO] GPS communicating, waiting for fix.");
  return true;
}

bool waitForNetwork()
{
  Serial.println("\n-> Waiting for Network Registration...");
  for (int i = 1; i <= 30; i++)
  {
    Serial.printf("\nNetwork Attempt %d/30: ", i);
    if (SimService::isNetworkConnected())
    {
      Serial.println("\n[SUCCESS] Registered to Network!");
      return true;
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
  return false;
}

// Passed as const String& to prevent memory fragmentation
bool JsonParsing(const String &response)
{
  int startIdx = response.indexOf('{');
  int endIdx = response.lastIndexOf('}');

  if (startIdx == -1 || endIdx == -1 || endIdx < startIdx)
  {
    Serial.println("[JSON] No valid JSON command found in response.");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response.substring(startIdx, endIdx + 1));

  if (error)
  {
    Serial.println("[JSON ERROR] Parsing failed.");
    return false;
  }

  if (doc["exec_command"] == "machine close")
  {
    Serial.println("\n[COMMAND] MACHINE CLOSE");
    isMachineKilled = true;
    digitalWrite(RELAY_PIN, LOW);
  }
  else if (doc["exec_command"] == "machine open")
  {
    Serial.println("\n[COMMAND] MACHINE OPEN");
    isMachineKilled = false;
    digitalWrite(RELAY_PIN, HIGH);
  }

  return true;
}

// Passed parameters as const reference to avoid string fragmentation
String generatePayload(const GpsData &myLocation, const String &signalStrength, bool is_contac_closed, bool gps_is_alive, bool is_wakeup_ping)
{
  JsonDocument doc;
  doc["id"] = DEVICE_ID;
  doc["lat"] = myLocation.lat;
  doc["lng"] = myLocation.lng;
  doc["alt"] = myLocation.alt;
  doc["spd"] = myLocation.speed;
  doc["crs"] = myLocation.course;     // Added Course (Heading)
  doc["ts"] = myLocation.timestamp;   // Added Timestamp
  doc["sat"] = myLocation.satellites; // Satellites count
  doc["hdop"] = myLocation.hdop;      // HDOP/Accuracy
  doc["sig"] = signalStrength;
  doc["icc"] = is_contac_closed ? 1 : 0;
  doc["gia"] = gps_is_alive ? 1 : 0;
  doc["val"] = myLocation.isValid ? 1 : 0;
  doc["wkp"] = is_wakeup_ping ? 1 : 0; // Wake-up detection flag

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void logDataForTest(const GpsData &myLocation)
{
  Serial.println("\n================ PREPARING REQUEST ================");
  Serial.printf("GPS Fixed: %s\n", myLocation.isValid ? "YES" : "NO");
  Serial.printf("Latitude : %.6f\n", myLocation.lat);
  Serial.printf("Longitude: %.6f\n", myLocation.lng);
  Serial.printf("Speed    : %.2f\n", myLocation.speed);
  Serial.printf("Course   : %.2f\n", myLocation.course);
  Serial.printf("Time     : %s\n", myLocation.timestamp.c_str());
  Serial.println("===================================================");
}

void sendToServerTask(void *pvParameters)
{
  bool first_loop = true;
  for (;;)
  {
    GpsData myLocation;
    bool is_contac_closed = TEST ? false : ignitionSensor.isTriggered();

    // TTFF (Time To First Fix) wait logic up to 2 minutes on timer wakeup ONLY
    if (first_loop && is_timer_wakeup)
    {
      Serial.println("\n[GPS] Waiting up to 2 minutes for Satellite Fix (Timer Wakeup)...");
      unsigned long startWait = millis();
      while (millis() - startWait < 120000)
      {
        if (xSemaphoreTake(gpsMutex, portMAX_DELAY) == pdTRUE)
        {
          myLocation = GpsService::getLocationData(gps, gps_is_alive);
          xSemaphoreGive(gpsMutex);
        }
        if (myLocation.isValid)
        {
          Serial.println("[GPS] Fix acquired!");
          break;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Poll every 1 second
      }
      if (!myLocation.isValid)
      {
        Serial.println("[GPS] Timeout! Proceeding with invalid coordinates to ping server.");
      }
    }
    else
    {
      if (xSemaphoreTake(gpsMutex, portMAX_DELAY) == pdTRUE)
      {
        myLocation = GpsService::getLocationData(gps, gps_is_alive);
        xSemaphoreGive(gpsMutex);
      }
    }

    first_loop = false; // Always clear on first pass

    String signalStrength = SimService::getSignalStrengthText();

    String payload = generatePayload(myLocation, signalStrength, is_contac_closed, gps_is_alive, is_wakeup_ping);
    is_wakeup_ping = false; // Reset after first ping
    logDataForTest(myLocation);

    Serial.println("[UDP PAYLOAD]: " + payload);

    String response = SimService::sendUdp(
        TEST ? TEST_SERVER_IP : SERVER_IP,
        TEST ? TEST_SERVER_PORT : SERVER_PORT,
        payload);
    Serial.println("[UDP RESPONSE]: " + response);

    JsonParsing(response);

    if (is_contac_closed)
    {
      goToDeepSleep();
    }

    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void setup()
{
  Serial.begin(115200);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
  {
    is_timer_wakeup = true;
    is_wakeup_ping = true;
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    is_wakeup_ping = true;
  }

  pinMode(CONTROL_PIN, OUTPUT);
  digitalWrite(CONTROL_PIN, HIGH);
  Serial.println("\nBooting... Waiting for Power Stabilization");
  delay(3000);

  gpsMutex = xSemaphoreCreateMutex();

  initSerial2AndGPS();
  checkSim800cStatus();
  gps_is_alive = checkGpsNeoStatus();
  waitForNetwork();

  xTaskCreatePinnedToCore(
      sendToServerTask,
      "ServerTask",
      10000,
      NULL,
      1,
      &ServerTask,
      0);
}

void loop()
{
  while (gpsSerial.available() > 0)
  {
    char c = gpsSerial.read();
    if (xSemaphoreTake(gpsMutex, portMAX_DELAY) == pdTRUE)
    {
      gps.encode(c);
      xSemaphoreGive(gpsMutex);
    }
  }
  vTaskDelay(1 / portTICK_PERIOD_MS);
}