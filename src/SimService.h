#ifndef SIM_SERVICE_5_H
#define SIM_SERVICE_5_H

#include <Arduino.h>

class SimService
{
public:
  // Using const String& for memory optimization to avoid fragmentation
  static String sendCommand(const String& cmd, int waitTime = 2000, const String& expectedResponse = "")
  {
    Serial2.print(cmd + "\r\n");
    unsigned long start = millis();
    String response = "";

    while (millis() - start < waitTime)
    {
      while (Serial2.available())
      {
        char c = Serial2.read();
        
        if (c >= 32 || c == '\r' || c == '\n') 
        {
          response += c;
        }
        
        Serial.write(c);
      }

      if (expectedResponse != "" && response.indexOf(expectedResponse) != -1)
      {
        break;
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    return response;
  }

  static String getSignalStrengthText()
  {
    Serial.println("\n[SimService] Getting Signal Strength...");
    String response = sendCommand("AT+CSQ", 1500, "OK");

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
    String response = sendCommand("AT", 2000, "OK");
    return response.indexOf("OK") != -1;
  }

  static bool isNetworkConnected()
  {
    Serial.println("\n[SimService] Checking SIM card status...");
    while (Serial2.available())
    {
      Serial2.read();
    }
    
    sendCommand("AT+CSQ", 1500, "OK");
    sendCommand("AT+COPS?", 1500, "OK");
    String simStatus = sendCommand("AT+CPIN?", 3000, "OK");

    static int simFailCounter = 0;

    if (simStatus.indexOf("READY") == -1)
    {
      Serial.println("[SimService] ERROR: SIM Card not found or Locked!");
      simFailCounter++;
      
      if (simFailCounter >= 10)
      {
        Serial.println("[SimService] CRITICAL: SIM failed 10 times. Resetting ESP...");
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
    String checkIP = sendCommand("AT+CIFSR", 3000, "."); 

    if (checkIP.indexOf("ERROR") != -1 || checkIP.length() < 7)
    {
      Serial.println("\n[SimService] Reconnecting to GPRS...");
      sendCommand("AT+CIPSHUT", 2000, "SHUT OK");
      sendCommand("AT+CGATT=1", 3000, "OK");
      sendCommand("AT+CSTT=\"mobinilweb\",\"\",\"\"", 3000, "OK");
      sendCommand("AT+CIICR", 5000, "OK");
      
      String ip = sendCommand("AT+CIFSR", 3000, ".");
      if (ip.indexOf("ERROR") != -1 || ip.length() < 7)
      {
        return false;
      }
    }
    return true;
  }

  static bool ensureUdpConnection(const String& serverIp, const String& port, bool forceReconnect = false)
  {
    static bool isUdpConnected = false;

    if (!ensureGPRS()) {
      isUdpConnected = false;
      return false;
    }

    if (isUdpConnected && !forceReconnect) {
      return true;
    }

    Serial.println("\n[SimService] Opening persistent UDP connection...");
    sendCommand("AT+CIPCLOSE", 1000); // تنظيف أي اتصال معلق

    String connCmd = "AT+CIPSTART=\"UDP\",\"" + serverIp + "\",\"" + port + "\"";
    String connRes = sendCommand(connCmd, 5000, "CONNECT OK");

    if (connRes.indexOf("CONNECT OK") != -1 || connRes.indexOf("ALREADY CONNECT") != -1)
    {
      isUdpConnected = true;
      return true;
    }
    
    isUdpConnected = false;
    return false;
  }

  static void checkUdpFailures(int &counter)
  {
    Serial.printf("\n[SimService] UDP Failures: %d/8\n", counter);
    if (counter >= 8)
    {
      Serial.println("\n[SimService] CRITICAL: UDP Failures! Rebooting...");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      ESP.restart();
    }
  }

  static String sendUdp(const String& serverIp, const String& port, const String& payload)
  {
    static int udpFailCounter = 0;
    
    if (!ensureUdpConnection(serverIp, port))
    {
      udpFailCounter++;
      checkUdpFailures(udpFailCounter);
      return "Request Failed: Cannot establish UDP.";
    }

    String sendCmd = "AT+CIPSEND=" + String(payload.length());
    String prompt = sendCommand(sendCmd, 2000, ">");

    if (prompt.indexOf(">") != -1)
    {
      Serial2.print(payload);
      String sendRes = sendCommand("", 5000, "SEND OK"); 

      if (sendRes.indexOf("SEND OK") != -1)
      {
        udpFailCounter = 0;
        
        String serverResult = "";
        unsigned long startWait = millis();
        while (millis() - startWait < 3000)
        {
           while (Serial2.available())
           {
             char c = Serial2.read();
             if (c >= 32 || c == '\r' || c == '\n' || c == '{' || c == '}') {
                serverResult += c;
             }
             Serial.write(c);
           }
           if (serverResult.indexOf("}") != -1) {
             break;
           }
           vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        
        return serverResult.length() > 0 ? serverResult : "UDP Data Sent.";
      }
    }
    
    Serial.println("\n[SimService] UDP Send Failed! Connection dropped. Forcing reconnect...");
    sendCommand("AT+CIPCLOSE", 1000);
    ensureUdpConnection(serverIp, port, true); // تحديث الحالة لـ Disconnected وطلب فتح جديد
    
    udpFailCounter++;
    checkUdpFailures(udpFailCounter);
    return "Request Failed at CIPSEND.";
  }
};

#endif