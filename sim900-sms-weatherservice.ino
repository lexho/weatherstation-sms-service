#include <sim900.h>
#include "rtctime.h"
#include "messagebuffer.h"
#include "commandbuffer.h"

bool led_state = LOW;
unsigned long start_time_buffer = 0;
unsigned long sim900_sync_time = 0;
unsigned long rtc_sync_time = 0; // For rollover-safe daily tasks
bool daily_task_done = false; // Flag to ensure daily invalidation runs only once

CommandBuffer cmdbuffer;

MessageBuffer buffer;
unsigned long message_received_time = 0; // in milliseconds since midnight
bool has_valid_data = false;
unsigned long calling_time = 0;

SIM900 sim900(Serial1);

int event_status_old = 0;
bool calling = false;
unsigned long lastRingMessageTime = 0;

// ---------------------------------------------------------------------------
// [NEW] "settime YYYY-MM-DD HH:MM:SS" via USB serial (e.g. /dev/ttyACM1)
// ---------------------------------------------------------------------------
enum LineMode { LM_START, LM_MATCHING, LM_SETTIME, LM_PASS };
static LineMode lineMode = LM_START;
static char lineBuf[40];
static uint8_t lineLen = 0;
static bool swallowLF = false;           // swallow the '\n' of a CRLF after settime
static const char SETTIME_CMD[] = "settime";
static const uint8_t SETTIME_LEN = sizeof(SETTIME_CMD) - 1;


void handleCommandChar(char c);          // existing command processing (below)
void handleSerialChar(char c);
void handleSetTime(const char* args);
bool sendBufferedMessageViaSMS();
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(19200);
  Serial1.begin(9600);
  while(!Serial);
  while(!Serial1);
  while(!sim900.bootstrap()) {
    delay(5000); // Wait before retrying
  }

  SIM900RTC current = sim900.rtc();
  Time::storeRTC(current);
  Time::printRTC(current);

  Serial.println(F("waiting for incoming data..."));
  Serial.println(F("set clock (local time) with: settime YYYY-MM-DD HH:MM:SS"));
}

void loop() {
  digitalWrite(STATUS_LED, led_state); led_state = !led_state;

  // Every 2 seconds print the message in the buffer
  if (millis() - start_time_buffer >= 2000) {
    start_time_buffer = millis();
    buffer.printBuffer();
  }

  // every 10 minutes
  if(millis() - sim900_sync_time >= Time::oneMinute * 10UL) {
    if(!sim900.handshake()) { // we lost connection to SIM900-Module
      while(!sim900.bootstrap()) { // reconnect
        delay(5000);
      }
    }
    sim900_sync_time = millis();
  }

  // This block runs approximately every hour to sync time.
  unsigned long now = millis();
  if (now - rtc_sync_time >= Time::oneHour) {
    Serial.println("syncing time with SIM900 RTC.");
    SIM900RTC current = sim900.rtc();
    Time::storeRTC(current);
    bool handshake_ok = true;
    if(!sim900.handshake()) {
      handshake_ok = false;
      while(!sim900.bootstrap()) {
        delay(5000); // Wait before retrying
      }
    }
    if(handshake_ok) rtc_sync_time = now; // only update timestamp, if handshake didn't fail.
  }

  // Invalidate the data once per day after 23:00.
  if (Time::getMillisSinceMidnight() > 23UL * Time::oneHour && !daily_task_done) {
    has_valid_data = false;
    daily_task_done = true;
  } else if (Time::getMillisSinceMidnight() < 23UL * Time::oneHour) {
    daily_task_done = false;
  }

  /*while(Serial1.available()) {
    Serial.write(Serial1.read());
  }*/

  SIM900_Handler_Event event = sim900.handleEvents();
  if(event.status != event_status_old) {
    Serial.print("event.status: "); Serial.println(event.status);
    event_status_old = event.status;
  }
  if(event.status == SIM900_OK) { Serial.println(F("received OK.")); }
  // If a RING event is detected, set the 'calling' state.
  if (event.status == SIM900_RING) {
   if (!sim900.isCalling()) { // On the very first ring, set the state and start time.
      sim900.calling = true;
      lastRingMessageTime = millis();
      Serial.println(F("someone is calling..."));
    }
    
    if(sim900.isCalling() && (millis() - lastRingMessageTime > 1000)) {
      Serial.print(sim900.getPhoneNumber()); Serial.println(F(" is calling..."));
      lastRingMessageTime = millis();
    }

    // put your code in here

  }
  if(event.status == SIM900_CLIP) {
    
  }
  if (event.status == SIM900_CMT) {
    // Clear the serial buffer to discard the SMS message body, as we don't need it.
    sim900.clearBuffer();
    Serial.print(F("received an sms from "));
    Serial.println(sim900.getPhoneNumber());
    sendBufferedMessageViaSMS();

    // put your code in here

  }
  if (event.status == SIM900_NOCARRIER) {
    if(sim900.isCalling()) {
      sim900.calling = false;
      Serial.println(F("call ended. preparing to send sms..."));
      sendBufferedMessageViaSMS();
    }

    // put your code in here
    
  }

  // Non-blocking serial command reader
  while (Serial.available() > 0) {
    handleSerialChar(Serial.read());
  }

  delay(50); // Reduced delay to make loop more responsive
}

// ---------------------------------------------------------------------------
// [NEW] Serial input front end.
// Forwards every character to the existing CommandBuffer, except lines that
// start with "settime": those are handled here. Characters that might still
// turn out to be the beginning of "settime" are held back (max. 7) and
// forwarded as soon as the line proves to be something else.
// ---------------------------------------------------------------------------
static void flushHeld() {
  if (lineMode == LM_MATCHING) {
    for (uint8_t i = 0; i < lineLen; i++) handleCommandChar(lineBuf[i]);
    lineLen = 0;
  }
}

void handleSerialChar(char c) {
  if (swallowLF) {
    swallowLF = false;
    if (c == '\n') return;
  }

  const bool eol = (c == '\n' || c == '\r');

  if (eol) {
    if (lineMode == LM_SETTIME) {
      lineBuf[lineLen] = '\0';
      handleSetTime(lineBuf);
      swallowLF = (c == '\r');
    } else {
      flushHeld();
      handleCommandChar(c);
    }
    lineLen = 0;
    lineMode = LM_START;
    return;
  }

  switch (lineMode) {
    case LM_START:
    case LM_MATCHING:
      lineMode = LM_MATCHING;
      if (tolower(c) == SETTIME_CMD[lineLen]) {
        lineBuf[lineLen++] = c;
        if (lineLen == SETTIME_LEN) { // full keyword seen -> collect arguments
          lineMode = LM_SETTIME;
          lineLen = 0;
        }
      } else {
        flushHeld();
        handleCommandChar(c);
        lineMode = LM_PASS;
      }
      break;
    case LM_SETTIME:
      if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
      break;
    case LM_PASS:
      handleCommandChar(c);
      break;
  }
}

static bool isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static bool validDateTime(int y, int mo, int d, int h, int mi, int s) {
  static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12) return false;
  int maxDay = dim[mo - 1] + ((mo == 2 && isLeapYear(y)) ? 1 : 0);
  if (d < 1 || d > maxDay) return false;
  return h >= 0 && h < 24 && mi >= 0 && mi < 60 && s >= 0 && s < 60;
}

// Day of week, 0 = Sunday (Sakamoto)
static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Offset to UTC in quarter hours for Central Europe, as the SIM900 stores it
// in AT+CCLK: 4 = CET (UTC+1), 8 = CEST (UTC+2). Input is local time.
static int euOffsetQuarterHours(int y, int mo, int d, int h) {
  if (mo < 3 || mo > 10) return 4;
  if (mo > 3 && mo < 10) return 8;
  int lastSunday = 31 - dayOfWeek(y, mo, 31);   // March and October have 31 days
  if (mo == 3) {
    if (d > lastSunday) return 8;
    if (d == lastSunday) return h >= 2 ? 8 : 4; // 02:00 CET -> 03:00 CEST
    return 4;
  }
  if (d < lastSunday) return 8;                 // October
  if (d == lastSunday) return h < 3 ? 8 : 4;    // 03:00 CEST -> 02:00 CET
  return 4;
}

void handleSetTime(const char* args) {
  int y, mo, d, h, mi, s;
  if (sscanf(args, " %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6 ||
      !validDateTime(y, mo, d, h, mi, s)) {
    Serial.println(F("error: usage: settime YYYY-MM-DD HH:MM:SS  (local time)"));
    return;
  }

  SIM900RTC cfg;
  cfg.year   = y % 100;
  cfg.month  = mo;
  cfg.day    = d;
  cfg.hour   = h;
  cfg.minute = mi;
  cfg.second = s;
  cfg.gmt    = euOffsetQuarterHours(y, mo, d, h); // quarter hours, as AT+CCLK expects

  Serial.println(F("setting SIM900 RTC..."));
  if (!sim900.updateRtc(cfg)) {
    Serial.println(F("error: SIM900 rejected the new time."));
    return;
  }

  // Read the time back (blocks a few seconds) and hand it to the software clock.
  SIM900RTC current = sim900.rtc();
  Time::printRTC(current);
  long got  = current.hour * 3600L + current.minute * 60L + current.second;
  long want = h * 3600L + mi * 60L + s;
  long diff = labs(got - want);
  if (diff > 43200L) diff = 86400L - diff;     // across midnight
  if (current.year != cfg.year || current.month != cfg.month ||
      current.day != cfg.day || diff > 15) {
    Serial.println(F("error: read-back does not match, clock not stored."));
    return;
  }
  Time::storeRTC(current);
  rtc_sync_time = millis();                    // restart the hourly sync timer
  Serial.println(F("clock set."));
}

// ---------------------------------------------------------------------------
// Existing command processing, moved out of loop() unchanged.
// ---------------------------------------------------------------------------
void handleCommandChar(char c) {
  Message msg = cmdbuffer.read(c);

  // Only process messages when a full command has been entered (not INVALID)
  if (msg.type != INVALID) {
      switch (msg.type) {
          case STOREBUFFER:
              // Ensure the message isn't too long for our buffer
              if (msg.message != nullptr && strlen(msg.message) >= buffer.sizeOfBuffer()) {
                  Serial.print(F("error: message is too long\n"));
              } else if (msg.message != nullptr) {
                  // Safely copy the message to the global buffer
                  buffer.copyToBuffer(msg.message);
                  buffer.printBuffer();
                  if (msg.message_received_time > 0) {
                      message_received_time = msg.message_received_time;
                      has_valid_data = true;
                  }
              } // if message is null, it was an invalid storebuffer command, do nothing.
              break;
          case SENDSMS:
              if (msg.phonenumber != nullptr && msg.message != nullptr)
                  sim900.sendSMSRoutine(msg.phonenumber, msg.message);
              break;
          case EMPTY:
              // If the command was empty (e.g., just pressing Enter), print the buffer
              buffer.printBuffer();
              break;
          case TIME:
              // The 'time' command already prints to Serial inside CommandBuffer.
              // No action needed here.
              break;
      }
  }
}

bool sendBufferedMessageViaSMS() {
  // If we don't have valid data, send the "not available" message.
  if (!has_valid_data) {
    return sim900.sendSMSRoutine("No data available.");
  }

  unsigned long currentMillisSinceMidnight = Time::getMillisSinceMidnight();
  unsigned long age;

  // Calculate the age of the message, handling the midnight rollover case.
  if (currentMillisSinceMidnight >= message_received_time) {
    age = currentMillisSinceMidnight - message_received_time;
  } else {
    age = (Time::oneDay - message_received_time) + currentMillisSinceMidnight;
  }

  // Check if the age is less than 4 minutes.
  if (age < (4UL * Time::oneMinute)) {
    return sim900.sendSMSRoutine(buffer.c_str());
  } else {
    return sim900.sendSMSRoutine("Keine aktuellen Wetterdaten verfugbar.");
  }
}
