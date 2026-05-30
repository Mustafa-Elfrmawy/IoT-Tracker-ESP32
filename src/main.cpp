// {version 1.0.1} - Mapped with Mutex Protection & Safe Parsing 
// Mostafa ElFaramawy 
#include <Arduino.h>
#include <TinyGPS++.h>
#include "driver/gpio.h"
#include "SimService.h"
#include "GpsService.h"
#include <ArduinoJson.h>

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
SemaphoreHandle_t gpsMutex;
RTC_DATA_ATTR bool isMachineKilled = false;

bool gps_is_alive = false;


#define GPS_RX_PIN 19
#define GPS_TX_PIN 18
#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define SIM800C_DTR_PIN 5
#define IGNITION_PIN 4
#define RELAY_PIN 22

TaskHandle_t ServerTask;

byte gpsSleepCmd[] = {0xB5, 0x62, 0x02, 0x41, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x4D, 0x3B};

void initSerial2AndGPS()
{
  gpio_hold_dis((gpio_num_t)SIM800C_DTR_PIN);
  gpio_hold_dis((gpio_num_t)RELAY_PIN);

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  pinMode(SIM800C_DTR_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(IGNITION_PIN, INPUT_PULLUP);
  if (isMachineKilled) {
    digitalWrite(RELAY_PIN, LOW); 
  } else {
    digitalWrite(RELAY_PIN, HIGH);
  }

  digitalWrite(SIM800C_DTR_PIN, LOW);
  Serial2.println("AT+CSCLK=0");
  byte coldStart[] = {0xB5, 0x62, 0x06, 0x04, 0x04, 0x00, 0xFF, 0x87, 0x02, 0x00, 0x90, 0x6F};
  Serial.println("-> Sending Cold Start to GPS...");
  gpsSerial.write(coldStart, sizeof(coldStart));
  delay(2000); 
  delay(1000);
}

void checkSim800cStatus()
{
  Serial.println("-> Starting SIM Health Check...");
  for (uint8_t i = 0; i < 100; i++)
  {
    if (SimService::isAlive())
    {
      Serial.println("-> SIM Module is Ready!");
      return;
    }
    delay(500);
  }
  Serial2.println("AT+CFUN=1,1");
  delay(500);
  ESP.restart();
}

bool checkGpsNeoStatus()
{
  Serial.println("-> Performing Deep GPS Check...");
  unsigned long start = millis();
  uint32_t initialChars = gps.charsProcessed();

  while (millis() - start < 3000)
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
          Serial.println("-> [SUCCESS] GPS is alive and sending valid NMEA data.");
          return true;
        }
      }
    }
  }

  if (gps.charsProcessed() - initialChars < 10)
  {
    Serial.println("-> [CRITICAL ERROR] No data received from GPS.");
    return false;
  }
  return true;
}

// for test 
bool waitForNetwork()
{
  Serial.println("\n-> Waiting for Network Registration...");
  for (int i = 1; i <= 50; i++)
  {
    Serial.printf("============================================");
    Serial.printf("\nNetwork Attempt %d/50: ", i);

    if (SimService::isNetworkConnected())
    {
      Serial.println("\n[SUCCESS] Registered to Network!");
      return true;
    }
    delay(3000);
  }
  return false;
}

bool JsonParsing(String response)
{
  int startIdx = response.indexOf('{');
  int endIdx = response.lastIndexOf('}');

  if (startIdx == -1 || endIdx == -1 || endIdx < startIdx)
  {
    Serial.println("[JSON] No valid JSON found in response.");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response.substring(startIdx, endIdx + 1));

  if (error)
  {
    Serial.print("[JSON ERROR] Parsing failed: ");
    Serial.println(error.c_str());
    return false;
  }

  if (doc["exec_command"] == "machine close")
  {
    Serial.println("\n[COMMAND] Server says: MACHINE CLOSE! Cutting power now...");
    isMachineKilled = true;
    digitalWrite(RELAY_PIN, LOW);
  }
  else if (doc["exec_command"] == "machine open")
  {
    Serial.println("\n[COMMAND] Server says: MACHINE OPEN! Restoring power...");
    isMachineKilled = false;
    digitalWrite(RELAY_PIN, HIGH);
  }
  
  return true;
}

void sleepAllDevices()
{
  Serial.println("\n>>> TEST COMPLETE: Confirmed condition. Going to DEEP SLEEP! <<<");

  Serial2.println("AT+CSCLK=1");
  delay(200);
  digitalWrite(SIM800C_DTR_PIN, HIGH);

  gpsSerial.write(gpsSleepCmd, sizeof(gpsSleepCmd));

  gpio_hold_en((gpio_num_t)SIM800C_DTR_PIN);
  gpio_hold_en((gpio_num_t)RELAY_PIN);
  gpio_deep_sleep_hold_en();

  Serial.println(">>> GOING TO DEEP SLEEP (Wakes up in 30 mins OR if ACC condition changes) <<<");
  Serial.flush();

  esp_sleep_enable_timer_wakeup(30ULL * 60ULL * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)IGNITION_PIN, 0);

  esp_deep_sleep_start();
}

bool isContacClosed()
{
  if (digitalRead(IGNITION_PIN) == HIGH)
  {
    Serial.println("\n[Test Logic] ACC OFF Detected! Verifying for 5 seconds...");
    bool isReallyON = true;

    for (int i = 1; i <= 5; i++)
    {
      if (digitalRead(IGNITION_PIN) == LOW)
      {
        isReallyON = false;
        Serial.println("[Test Logic] False Alarm (Fluctuation). Resuming normal operation.");
        break;
      }
      Serial.printf("[Test Logic] Verifying... %d/5\n", i);
      delay(1000);
    }
    return isReallyON;
  }
    Serial.println("\n[Test Logic] ACC is On. Continuing normal tracking...");
    return false;
  
}

void sendToServerTask(void *pvParameters)
{
  for (;;)
  {
    GpsData myLocation;
    bool is_contac_closed = isContacClosed();

    if (xSemaphoreTake(gpsMutex, portMAX_DELAY) == pdTRUE)
    {
      myLocation = GpsService::getLocationData(gps, gps_is_alive);
      xSemaphoreGive(gpsMutex);
    }

    String signalStrength = SimService::getSignalStrengthText();
    
    String url = "http://[YOURDEVICE-OR-YOURSERVER-IP]/api/test?lat=" + String(myLocation.lat, 6) +
                 "&gia=" + String(gps_is_alive) + //
                 "&giv=" + String(myLocation.isValid) +
                 "&lng=" + String(myLocation.lng, 6) +
                 "&alt=" + String(myLocation.alt, 1) +
                 "&spd=" + String(myLocation.speed) +
                 "&sig=" + signalStrength +
                 "&icc=" + String(is_contac_closed);

    String response = SimService::sendHttp(url);
    Serial.println("\n[SERVER RESPONSE]: " + response);

    JsonParsing(response);

    if (is_contac_closed && !isMachineKilled)
    {
      sleepAllDevices();
    }

    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void setup()
{
  Serial.println("\n=====================================");
  Serial.println("System Booting: Tracking Started");
  Serial.println("=====================================");
  delay(2000);

  gpsMutex = xSemaphoreCreateMutex();

  initSerial2AndGPS();
  checkSim800cStatus();
  gps_is_alive = checkGpsNeoStatus();

  if (!waitForNetwork())
  {
    Serial.println("\n[CRITICAL ERROR] Connection failed. Restarting ESP...");
    ESP.restart();
  }

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
}