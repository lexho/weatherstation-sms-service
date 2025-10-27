#ifndef TIME_H
#define TIME_H

#include <cstdint>
#include <cstddef> // For size_t
#include "Arduino.h"
#include "sim900.h"

typedef struct _RTC {
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RTC;

class Time {
public:
    static const unsigned long oneDay;
    static const unsigned long oneHour;
    static const unsigned long twoHours;
    static const unsigned long oneMinute;
    static const unsigned long timezone;

    static uint8_t day;
    static uint8_t month;
    static uint8_t year;

    static unsigned long rtcSyncMillis; // millis() value at the last RTC sync
    static unsigned long utcMillisAtSync; // UTC time in milliseconds since midnight at the last sync

    static void storeRTC(SIM900RTC rtc);
    static unsigned long getMillisSinceMidnight();
    static bool isDST();
    static void getFakeHardwareClockTime(char* buffer, size_t bufferSize);

    static void printRTC(SIM900RTC datetime);

private:
    static uint8_t dayOfWeek(uint8_t y, uint8_t m, uint8_t d);
};

#endif // TIME_H