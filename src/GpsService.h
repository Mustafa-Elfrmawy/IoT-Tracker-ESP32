#ifndef GPS_SERVICE_5_H
#define GPS_SERVICE_5_H

#include <Arduino.h>
#include <TinyGPS++.h>

struct GpsData
{
    bool isValid;
    bool gps_is_alive;
    double lat;
    double lng;
    int satellites;
    double speed;
    double alt;
};

class GpsService
{
public:
    static bool isAlive(TinyGPSPlus &gps)
    {
        return gps.charsProcessed() > 10;
    }

    static GpsData getLocationData(TinyGPSPlus &gps, bool gps_is_alive)
    {
        GpsData data;
        data.isValid = gps.location.isValid();
        data.satellites = gps.satellites.value();

        if (data.isValid)
        {
            data.lat = gps.location.lat();
            data.lng = gps.location.lng();
            data.speed = gps.speed.kmph();
            data.alt = gps.altitude.meters();
            data.gps_is_alive = true;
        }
        else
        {
            data.lat = data.lng = data.speed = data.alt = 0.0;
            data.gps_is_alive = gps_is_alive;
        }
        return data;
    }

    static void resetModule(HardwareSerial &serial)
    {
        serial.println("$PUBX,40,RST,0,0,1,0*1C");
    }
};

#endif