#include <sim900.h>
#include "time.h"


bool led_state = LOW;

unsigned long start_time_buffer = 0;
char buffer[161]; // Max SMS length + null terminator
char buffer_old[161];
unsigned long message_age = 0; // in milliseconds since midnight
char phonenumber[14]; //+XXxxxxxxxxxx 
bool sendSMS = false;
bool calling = false;
unsigned long calling_time = 4294962000UL; // near to the but not the maximum value
bool smsSent = false;
unsigned long lastRingMessageTime = 0; // Timestamp for the last "ringing" message
int event_status_old = 0;
unsigned long rtcSyncTime = 0; // For rollover-safe daily tasks
//const unsigned long oneDay = 1000UL * 60 * 60 * 24; // Use UL to prevent int overflow during calculation
//const unsigned long oneMinute = 60UL * 1000; // 60,000 milliseconds

const byte serialCmdBufferSize = 255;
char serialCmdBuffer[serialCmdBufferSize];
byte serialCmdBufferIdx = 0;

/*uint8_t day;
uint8_t month;
uint8_t year;*/

uint8_t hour;
uint8_t minute;
uint8_t second;
//unsigned long rtcSyncMillis = 0; // millis() value at the last RTC sync
unsigned long rtcSyncMilliSeconds = 0; // Time in seconds since midnight at the last RTC sync

SIM900 sim900(Serial1);

void setup() {
  Serial.begin(19200);
  Serial1.begin(9600);
  while(!Serial);
  while(!Serial1);
  //sim900.bootstrap();
  while(!sim900.bootstrap()) {
    delay(5000); // Wait before retrying
  }

  SIM900RTC current = sim900.rtc();
  Time::storeRTC(current);
  Time::printRTC(current);

  Serial.println(F("waiting for incoming data..."));
}

void loop() {
  digitalWrite(STATUS_LED, led_state); led_state = !led_state;

  // Rollover-safe check if 5 seconds have passed
  if (millis() - start_time_buffer >= 5000) {
    if(strcmp(buffer, buffer_old) != 0 && strlen(buffer) > 0) { 
      Serial.print(F("buffer: "));
      Serial.println(buffer);
      strcpy(buffer_old, buffer);
    }
  }

// Update time once per day
  if (millis() - rtcSyncTime >= Time::oneHour) {
    Serial.println("sync time with sim900 rtc");
    if(Time::getMillisSinceMidnight() > 23UL*Time::oneHour) { // every day at 23:00
      Serial.println("invalidate message");
      message_age = 0; // reset message age
    }
    rtcSyncTime = millis();
    SIM900RTC current = sim900.rtc();
    Time::storeRTC(current);
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
    if (!calling) { // On the very first ring, set the state and start time.
      calling = true; 
      calling_time = millis();
      lastRingMessageTime = millis(); // Set initial time for the first message
    }
  }
  if(event.status == SIM900_CLIP) {
    //phonenumber = event.phonenumber.c_str();
    strncpy(phonenumber, event.phonenumber.c_str(), sizeof(phonenumber) - 1);
    phonenumber[sizeof(phonenumber) - 1] = '\0'; // Ensure null termination
    Serial.print(phonenumber);
    Serial.println(F(" is calling..."));
  }
  if (event.status == SIM900_CMT) {
    // An SMS has arrived. Capture the sender's phone number.
    strncpy(phonenumber, event.phonenumber.c_str(), sizeof(phonenumber) - 1);
    phonenumber[sizeof(phonenumber) - 1] = '\0'; // Ensure null termination

    // Clear the serial buffer to discard the SMS message body, as we don't need it.
    while(Serial1.available()) { Serial1.read(); }
    Serial.print(F("received an sms from "));
    Serial.println(phonenumber);
    sendSMS = true; // Trigger SMS for incoming text messages as before
  }
  if(event.status == SIM900_CMGS) { if(!smsSent) { Serial.println(F("sms sent.")); smsSent = true; } }
  if (event.status == SIM900_NOCARRIER && calling) {
    Serial.println(F("call ended. preparing to send sms..."));
    calling = false; // Reset the calling state
    sendSMS = true;  // Set the flag to send the SMS
  }
  // If we are in a call, print "someone is calling..." every 2 seconds.
  if (event.status == SIM900_RING && calling && (millis() - lastRingMessageTime > 1000)) {
    Serial.print(phonenumber); Serial.println(F(" is calling..."));
    lastRingMessageTime = millis();
  }
  if (sendSMS) {
    // Compare milliseconds to milliseconds. Convert the dataset age (in seconds) to milliseconds.
    if((message_age + Time::oneMinute*4) > Time::getMillisSinceMidnight()) {
      sim900.sendSMSRoutine(phonenumber, buffer);
    } else {
      sim900.sendSMSRoutine(phonenumber, "Keine aktuellen Wetterdaten verfugbar.");
    }
    sendSMS = false;
  }

  // Non-blocking serial command reader
  while (Serial.available() > 0) {
    char receivedChar = Serial.read();

    if (receivedChar == '\n' || receivedChar == '\r') {
      if (serialCmdBufferIdx > 0) { // If we have a command
        serialCmdBuffer[serialCmdBufferIdx] = '\0'; // Null-terminate the string
        parseCommand(serialCmdBuffer); // size 255
        serialCmdBufferIdx = 0; // Reset for next command
      }
    } else {
      if (serialCmdBufferIdx < serialCmdBufferSize - 1) {
        serialCmdBuffer[serialCmdBufferIdx++] = receivedChar;
      }
    }
  }

  delay(50); // Reduced delay to make loop more responsive
}

void parseCommand(const char* command) {
  const char* storebuffer_cmd = "storebuffer";
  if (strncmp(command, storebuffer_cmd, strlen(storebuffer_cmd)) == 0) {
    const char* message = command + strlen(storebuffer_cmd);

    // Check if there is data after "storebuffer". It should start with a space.
    if (*message != '\0' && *message != ' ') {
        // The command is something like "storebufferXYZ", which is invalid.
        return; 
    }
    if (*message == ' ') { // Skip the leading space if it exists.
        message++;
    }

    if (strlen(message) > 0) { // We have data to parse
      // Ensure the message isn't too long for our buffer
      if (strlen(message) >= sizeof(buffer)) {
        Serial.print(F("Error: message is too long\n"));
        return;
      }
      // Find "time: HH:MM:SS" and parse it
      const char* time_ptr = strstr(message, "time: ");
      if (time_ptr != NULL) {
        time_ptr += 6; // Move pointer past "time: " to the start of HH:MM:SS
        unsigned long total_seconds = ((time_ptr[0] - '0') * 10UL + (time_ptr[1] - '0')) * 3600UL +
                                      ((time_ptr[3] - '0') * 10UL + (time_ptr[4] - '0')) * 60UL +
                                      ((time_ptr[6] - '0') * 10UL + (time_ptr[7] - '0'));
        message_age = total_seconds * 1000UL;
      }
      // Safely copy the message to the global buffer
      strncpy(buffer, message, sizeof(buffer) - 1);
      buffer[sizeof(buffer) - 1] = '\0'; // GUARANTEE null termination
    } else {
      // The command was just "storebuffer" with no data. Print current buffer.
      Serial.print("current buffer: "); Serial.print(buffer); Serial.print('\n');
    }

  } else if (strcmp(command, "time") == 0) {
    char timeBuffer[25];
    Time::getFakeHardwareClockTime(timeBuffer, sizeof(timeBuffer));
    while(millis() % 1000 != 0); // print time at precise time intervals
    Serial.print("time: "); Serial.println(timeBuffer);
  } else if (strncmp(command, "sendsms", 7) == 0) {
    Serial.println(F("sendsms"));
    // The logic for sendsms using String is complex to convert without
    // more context on the exact format. Leaving as-is for now, but be
    // aware it still uses heap allocation.

    String cmdStr(command);
    int start = cmdStr.indexOf("sendsms") + 8;
    int end = cmdStr.indexOf(" ", start + 1);
    //String phonenumber = cmdStr.substring(start + 1, end);
    start = end + 1;
    end = cmdStr.indexOf('\n');
    String message1 = cmdStr.substring(start, end);

    char* space = strchr(command, ' ');
    char* phonenumber;
    //const char* message;
    if (space != NULL) {
      phonenumber = space+1;
      char* end = strchr(phonenumber+1, ' ');
      phonenumber[13] = '\0';
    }

    Serial.print(F("phonenumber: ")); Serial.println(phonenumber);
    Serial.print(F("message: ")); Serial.println(message1);
    const char* message = message1.c_str();
    if (sizeof(phonenumber) > 0 && message1.length() > 0) {
      sim900.sendSMSRoutine(phonenumber, message);
    } else {
      Serial.println(F("sms could not be sent."));
    }
  }  //else
}

/*void storeRTC(SIM900RTC rtc) {
  day = rtc.day;
  month = rtc.month;
  year = rtc.year;

  // Store the time from RTC and the millis() value at the time of sync
  rtcSyncMillis = millis();
  // Convert total seconds since midnight to milliseconds for the base time
  rtcSyncMilliSeconds = ((unsigned long)rtc.hour * 3600UL + (unsigned long)rtc.minute * 60UL + (unsigned long)rtc.second) * 1000UL;

  // For debug printing, we can still use the individual components
  Serial.print(F("stored datetime: "));
  printRTC(rtc);
}*/

// hour:minute:seconds
/*void getFakeHardwareClockTime(char* buffer, size_t bufferSize) {
  // This function still works with seconds for display purposes
  unsigned long now_seconds = Time::getMillisSinceMidnight() / 1000UL;
  
  int s = now_seconds % 60;
  unsigned long total_minutes = now_seconds / 60;
  int min = total_minutes % 60;
  unsigned long total_hours = total_minutes / 60;
  int h = total_hours % 24;

  snprintf(buffer, bufferSize,
           "%02u.%02u.%02u %02u:%02u:%02u",
           day, month, year,
           h, min, s);
}*/

// returns milliseconds since midnight
/*unsigned long getMillisSinceMidnight() {
  // Calculate elapsed seconds since last sync, safe from millis() rollover
  unsigned long elapsedMillis = millis() - rtcSyncMillis;
  return (rtcSyncMilliSeconds + elapsedMillis) % Time::oneDay;
}*/

/*void printRTC(SIM900RTC datetime) {
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
}*/