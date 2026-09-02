#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// Base Sensor Interface to respect Open/Closed Principle
class Sensor
{
public:
    virtual ~Sensor() {}
    virtual void init() = 0;
    virtual String readValue() = 0;
    virtual bool isTriggered() = 0;
};

// Ignition Sensor implementation
class IgnitionSensor : public Sensor
{
private:
    uint8_t pin;
public:
    IgnitionSensor(uint8_t pinNumber) : pin(pinNumber) {}
    
    void init() override
    {
        pinMode(pin, INPUT_PULLUP);
    }

    String readValue() override
    {
        return String(digitalRead(pin));
    }

    bool isTriggered() override
    {
        // Simple debouncing logic for Ignition
        if (digitalRead(pin) == HIGH) 
        {
            Serial.println("\n[Logic] ACC OFF Detected. Verifying...");
            bool isReallyOFF = true;
            for (int i = 1; i <= 5; i++)
            {
                if (digitalRead(pin) == LOW)
                {
                    Serial.println("[Logic] False Alarm.");
                    isReallyOFF = false;
                    break;
                }
                vTaskDelay(1000 / portTICK_PERIOD_MS); // FreeRTOS safe delay
            }
            return isReallyOFF;
        }
        return false;
    }
};

#endif

