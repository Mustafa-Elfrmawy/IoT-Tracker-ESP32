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
    int satellites;     // Satellites Count
    double speed;
    double alt;
    double course;      // Added course/heading
    double hdop;        // Added HDOP / Accuracy
    String timestamp;   // Added timestamp
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
            data.course = gps.course.isValid() ? gps.course.deg() : 0.0;
            data.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.9;
            data.gps_is_alive = true;
        }
        else
        {
            data.lat = data.lng = data.speed = data.alt = data.course = 0.0;
            data.hdop = 99.9;
            data.gps_is_alive = gps_is_alive;
        }

        if (gps.date.isValid() && gps.time.isValid())
        {
            char ts[25];
            snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     gps.date.year(), gps.date.month(), gps.date.day(),
                     gps.time.hour(), gps.time.minute(), gps.time.second());
            data.timestamp = String(ts);
        }
        else
        {
            data.timestamp = "";
        }

        return data;
    }
};

#endif