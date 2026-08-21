#include <Arduino.h>
#include <TinyGPS++.h>
#include "SimService.h" 
#include "GpsService.h" 
#include <ArduinoJson.h>
#include <esp_sleep.h>

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
SemaphoreHandle_t gpsMutex;

RTC_DATA_ATTR bool isMachineKilled = false; 

bool gps_is_alive = false;

#define GPS_RX_PIN 19
#define GPS_TX_PIN 18
#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define IGNITION_PIN 22
#define RELAY_PIN 4
#define CONTROL_PIN 2 

#define uS_TO_S_FACTOR 1000000ULL  
#define TIME_TO_SLEEP  1200        

TaskHandle_t ServerTask;

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
  pinMode(IGNITION_PIN, INPUT_PULLUP);

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

bool JsonParsing(String response)
{
  int startIdx = response.indexOf('{');
  int endIdx = response.lastIndexOf('}');

  if (startIdx == -1 || endIdx == -1 || endIdx < startIdx)
  {
    Serial.println("[JSON] No valid JSON found.");
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

bool isContacClosed()
{
  return false;
  // if (digitalRead(IGNITION_PIN) == HIGH) 
  // {
  //   Serial.println("\n[Logic] ACC OFF Detected. Verifying...");
  //   bool isReallyOFF = true;
  //   for (int i = 1; i <= 5; i++)
  //   {
  //     if (digitalRead(IGNITION_PIN) == LOW)
  //     {
  //       Serial.println("[Logic] False Alarm.");
  //       isReallyOFF = false;
  //       break;
  //     }
  //     vTaskDelay(1000 / portTICK_PERIOD_MS);
  //   }
  //   return isReallyOFF;
  // }
  // return false;
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
    
    Serial.println("\n================ PREPARING REQUEST ================");
    Serial.printf("GPS Fixed: %s\n", myLocation.isValid ? "YES" : "NO");
    Serial.printf("Latitude : %.6f\n", myLocation.lat);
    Serial.printf("Longitude: %.6f\n", myLocation.lng);
    Serial.printf("Speed    : %.2f\n", myLocation.speed);
    Serial.println("===================================================");
    String route = "http://161.97.84.206:8070";
    // String route = "http://gps-traker.myacademy.tech";
    String url = route + "/api/tracker-to-server?lat=" + String(myLocation.lat, 6) +
                 "&gia=" + String(gps_is_alive) +
                 "&giv=" + String(myLocation.isValid) +
                 "&lng=" + String(myLocation.lng, 6) +
                 "&alt=" + String(myLocation.alt, 1) +
                 "&spd=" + String(myLocation.speed) +
                 "&sig=" + signalStrength +
                 "&icc=" + String(is_contac_closed);

    Serial.println("[URL]: " + url);
    String response = SimService::sendHttp(url);
    Serial.println("[RESPONSE]: " + response);

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