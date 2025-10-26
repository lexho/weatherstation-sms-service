#include <Arduino.h>
#include "time.h"

const unsigned long Time::oneMinute = 60UL * 1000; // 60,000 milliseconds
const unsigned long Time::oneHour = oneMinute * 60;
const unsigned long Time::twoHours = oneHour * 2;
const unsigned long Time::oneDay = oneHour * 24;

// Initialize static member variables
uint8_t Time::day = 0;
uint8_t Time::month = 0;
uint8_t Time::year = 0;
unsigned long Time::rtcSyncMillis = 0;
unsigned long Time::rtcMilliSeconds = 0; // Time in milliseconds since midnight

/**
 * @brief Calculates the day of the week from a given date.
 * 
 * This uses Sakamoto's algorithm.
 * @param y Year (e.g., 24 for 2024)
 * @param m Month (1-12)
 * @param d Day (1-31)
 * @return uint8_t Day of the week (0=Sunday, 1=Monday, ..., 6=Saturday)
 */
uint8_t Time::dayOfWeek(uint8_t y, uint8_t m, uint8_t d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t year = y + 2000; // Assuming years are 00-99 for 2000-2099
    if (m < 3) year--;
    return (year + year/4 - year/100 + year/400 + t[m-1] + d) % 7;
}

// Store the time from RTC and the millis() value at the time of sync
void Time::storeRTC(SIM900RTC rtc)
{
    Time::day = rtc.day;
    Time::month = rtc.month;
    Time::year = rtc.year;
    Time::rtcSyncMillis = millis(); // last rtc sync in milliseconds since program start
    rtc.hour = (rtc.hour - 2) % 24;
    Time::rtcMilliSeconds = ((unsigned long)rtc.hour * 3600UL + (unsigned long)rtc.minute * 60UL + (unsigned long)rtc.second) * 1000UL;

    /*Serial.println("storeRTC: ");
    Serial.print("hour (corrected): ");
    Serial.println(rtc.hour);
    Serial.print("rtcSyncMillis: ");
    Serial.println(Time::rtcSyncMillis);
    Serial.print("Time::rtcMilliSeconds: ");
    Serial.println(Time::rtcMilliSeconds);*/
}

unsigned long Time::getMillisSinceMidnight() {
    unsigned long elapsedMillis = millis() - Time::rtcSyncMillis;
    return (Time::rtcMilliSeconds + elapsedMillis) % Time::oneDay;
}

/**
 * @brief Determines if the current date/time is in Central European Summer Time (Sommerzeit).
 * 
 * Assumes the time from the SIM module is UTC.
 * Sommerzeit: Last Sunday in March (2:00 UTC becomes 3:00 CEST) to last Sunday in October (3:00 CEST becomes 2:00 CET).
 * In UTC, this is from 1:00 UTC on the last Sunday of March to 1:00 UTC on the last Sunday of October.
 * @return true if it is Sommerzeit (DST), false otherwise.
 */
bool Time::isDST() {
    if (month < 3 || month > 10) return false; // Not DST in Jan, Feb, Nov, Dec
    if (month > 3 && month < 10) return true;  // DST in Apr, May, Jun, Jul, Aug, Sep

    unsigned long utcMillis = getMillisSinceMidnight();
    uint8_t utcHour = (utcMillis / Time::oneHour) % 24;

    // Find the last Sunday of the month
    uint8_t lastDayOfMonth = 31;
    if (month == 3) lastDayOfMonth = 31;
    if (month == 10) lastDayOfMonth = 31;
    
    uint8_t dow = dayOfWeek(year, month, lastDayOfMonth); // Day of week for the 31st
    uint8_t lastSunday = lastDayOfMonth - dow;

    if (month == 3) { // March
        if (day > lastSunday) return true;
        if (day == lastSunday) return utcHour >= 1; // DST starts at 1:00 UTC
        return false;
    }

    if (month == 10) { // October
        if (day < lastSunday) return true;
        if (day == lastSunday) return utcHour < 1; // DST ends at 1:00 UTC
        return false;
    }

    return false; // Should not be reached
}

// hour:minute:seconds
void Time::getFakeHardwareClockTime(char* buffer, size_t bufferSize) {
  // Get UTC time and add offset for local time (CET/CEST)
  unsigned long utcMillis = getMillisSinceMidnight();
  unsigned long localMillis = utcMillis + (isDST() ? Time::twoHours : Time::oneHour);
  unsigned long localSeconds = (localMillis / 1000UL) % (24 * 3600UL);

  uint8_t h = localSeconds / 3600;
  uint8_t min = (localSeconds % 3600) / 60;
  uint8_t s = localSeconds % 60;

  snprintf(buffer, bufferSize, "%02u.%02u.%02u %02u:%02u:%02u", Time::day, Time::month, Time::year, h, min, s);
}

void Time::printRTC(SIM900RTC datetime) {
  int day = datetime.day;
  int month = datetime.month;
  int year = datetime.year;

  int h = datetime.hour;
  int m = datetime.minute;
  int s = datetime.second;

  Serial.print("rtc: ");
  Serial.print(day>9 ? "" : "0"); Serial.print(day); Serial.print(".");
  Serial.print(month>9 ? "" : "0"); Serial.print(month); Serial.print(".");
  Serial.print(year>9 ? "" : "0"); Serial.print(year); Serial.print(" ");

  Serial.print(h>9 ? "" : "0"); Serial.print(h); Serial.print(":");
  Serial.print(m>9 ? "" : "0"); Serial.print(m); Serial.print(":");
  Serial.print(s>9 ? "" : "0"); Serial.println(s);
}