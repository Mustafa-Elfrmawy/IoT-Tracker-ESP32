#ifndef SIM_SERVICE_5_H
#define SIM_SERVICE_5_H

#include <Arduino.h>

class SimService
{
public:
  static String sendCommand(String cmd, int waitTime = 1000, String expectedResponse = "")
  {
    Serial2.print(cmd + "\r\n");
    unsigned long start = millis();
    String response = "";

    while (millis() - start < waitTime)
    {
      while (Serial2.available())
      {
        char c = Serial2.read();
        response += c;
        Serial.write(c);
      }

      if (expectedResponse != "" && response.indexOf(expectedResponse) != -1)
      {
        break;
      }
      vTaskDelay(1);
    }
    return response;
  }

  static String getSignalStrengthText()
  {
    Serial.println("\n[SimService] Getting Signal Strength...");
    String response = sendCommand("AT+CSQ", 1000, "OK");

    int startIndex = response.indexOf("+CSQ: ");
    if (startIndex != -1)
    {
      startIndex += 6;
      int commaIndex = response.indexOf(",", startIndex);

      if (commaIndex != -1)
      {
        String signalValue = response.substring(startIndex, commaIndex);
        signalValue.trim();
        return signalValue;
      }
    }
    return "99";
  }

  static bool isAlive()
  {
    Serial.println("\n[SimService] Checking if module is alive...");
    String response = sendCommand("AT", 1 * 1000, "OK");
    sendCommand("AT+GSN", 1 * 1000, "OK");
    return response.indexOf("OK") != -1;
  }

  static bool isNetworkConnected()
  {
    Serial.println("\n[SimService] Checking SIM card status...");
    while (Serial2.available())
    {
      Serial2.read();
    }
    sendCommand("AT+CSQ", 1000, "OK");
    String simStatus = sendCommand("AT+CPIN?", 2000, "OK");

    static int simFailCounter = 0;

    if (simStatus.indexOf("READY") == -1)
    {
      Serial.println("[SimService] ERROR: SIM Card not found or Locked!");
      simFailCounter++;

      if (simFailCounter >= 10)
      {
        Serial.println("[SimService] CRITICAL: SIM failed 10 times. Resetting Module...");
        Serial2.println("AT+CFUN=1,1");
        delay(10 * 1000);
        simFailCounter = 0;
        ESP.restart();
      }
      return false;
    }

    simFailCounter = 0;
    String response = sendCommand("AT+CREG?", 2000, "OK");

    if (response.indexOf(",1") != -1 || response.indexOf(",5") != -1)
    {
      Serial.println("[SimService] Network Connected successfully.");
      return true;
    }
    return false;
  }

  static bool ensureGPRS()
  {
    String checkIP = sendCommand("AT+SAPBR=2,1", 2000, "OK");

    if (checkIP.indexOf("0.0.0.0") != -1 || checkIP.indexOf("ERROR") != -1)
    {
      Serial.println("\n[SimService] Reconnecting to GPRS...");
      sendCommand("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", 1000, "OK");
      sendCommand("AT+SAPBR=3,1,\"APN\",\"mobinilweb\"", 1000, "OK");

      String connectRes = sendCommand("AT+SAPBR=1,1", 15000, "OK");
      if (connectRes.indexOf("ERROR") != -1)
      {
        return false;
      }
    }
    return true;
  }

  static void checkHttpFailures(int &counter)
  {
    Serial.printf("\n[SimService] HTTP Failures: %d/3\n", counter);
    if (counter >= 3)
    {
      Serial.println("\n[SimService] CRITICAL: HTTP Failures! Rebooting...");
      sendCommand("AT+CFUN=1,1", 2000, "OK");
      delay(10000);
      ESP.restart();
    }
  }

  static String sendHttp(String url)
  {
    static int httpFailCounter = 0;
    if (!ensureGPRS())
    {
      httpFailCounter++;
      checkHttpFailures(httpFailCounter);
      return "Request Failed: No GPRS.";
    }

    sendCommand("AT+HTTPTERM", 500);
    delay(100);

    if (sendCommand("AT+HTTPINIT", 2000, "OK").indexOf("ERROR") != -1)
    {
      sendCommand("AT+HTTPTERM", 500);
      httpFailCounter++;
      checkHttpFailures(httpFailCounter);
      return "Request Failed at HTTPINIT.";
    }

    sendCommand("AT+HTTPPARA=\"CID\",1", 1000, "OK");
    sendCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", 1000, "OK");
    delay(500);

    sendCommand("AT+HTTPACTION=0", 2000, "OK");

    String actionRes = "";
    unsigned long startWait = millis();
    while (millis() - startWait < 15000)
    {
      while (Serial2.available())
      {
        char c = Serial2.read();
        actionRes += c;
        Serial.print(c);
      }
      if (actionRes.indexOf("+HTTPACTION:") != -1 && actionRes.indexOf("\n", actionRes.indexOf("+HTTPACTION:")) != -1)
      {
        break;
      }
      vTaskDelay(1);
    }

    String serverResult = "";
    if (actionRes.indexOf("200") != -1 || actionRes.indexOf("201") != -1)
    {
      serverResult = sendCommand("AT+HTTPREAD", 3000, "OK");
      httpFailCounter = 0;
    }
    else
    {
      serverResult = "Request Failed.";
      httpFailCounter++;
      checkHttpFailures(httpFailCounter);
    }

    sendCommand("AT+HTTPTERM", 1000, "OK");
    return serverResult;
  }
};

#endif