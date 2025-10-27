#include <sim900.h>
#include "time.h"
#include "messagebuffer.h"
#include "commandbuffer.h"

bool led_state = LOW;
unsigned long start_time_buffer = 0;
unsigned long sim900_sync_time = 0;
unsigned long rtc_sync_time = 0; // For rollover-safe daily tasks
bool daily_task_done = false; // Flag to ensure daily invalidation runs only once

CommandBuffer cmdbuffer;


SIM900 sim900(Serial1);

class OnOKListener : public EventListener {
public:
  OnOKListener() { type = SIM900_OK; }
  bool execute() override { Serial.println(F("received OK.")); }
};

class OnCallListener : public EventListener {
  unsigned long lastRingMessageTime = 0;
public:
    OnCallListener() {
      type = SIM900_RING; // Set the type inherited from EventListener
    }

    bool execute() override {

      // put your code in here

        // This code will now be executed from within sim900.handleEvents()
        if (!sim900.isCalling()) { // On the very first ring, set the state and start time.
          sim900.calling = true;
          lastRingMessageTime = millis();
          Serial.println(F("someone is calling..."));
        }
        
        if(sim900.isCalling() && (millis() - lastRingMessageTime > 1000)) {
          Serial.print(sim900.getPhoneNumber()); Serial.println(F(" is calling..."));
          lastRingMessageTime = millis();
        }
        return true;
    }
};

class OnNOCARRIERListener : public EventListener {
public:
  OnNOCARRIERListener() {
    type = SIM900_NOCARRIER;
  }

  MessageBuffer buffer;
  unsigned long message_received_time = 0; // in milliseconds since midnight
  bool has_valid_data = false;

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

  bool execute() override {

    // put your code in here

    if(sim900.isCalling()) {
      sim900.calling = false;
      Serial.println(F("call ended. preparing to send sms..."));
      sendBufferedMessageViaSMS();
    }
  }
};

OnNOCARRIERListener* onNocarrierPtr = nullptr;

class OnCMTListener : public EventListener {
public:
  OnCMTListener() { type = SIM900_CMT; }
  bool execute() override {

    // put your code in here 
    
    // Clear the serial buffer to discard the SMS message body, as we don't need it.
    sim900.clearBuffer();
    Serial.print(F("received an sms from "));
    Serial.println(sim900.getPhoneNumber());
    onNocarrierPtr->sendBufferedMessageViaSMS();
  }
};

void setup() {
  Serial.begin(19200);
  Serial1.begin(9600);
  while(!Serial);
  while(!Serial1);
  while(!sim900.bootstrap()) {
    delay(5000); // Wait before retrying
  }

  // Create and register the listener so it's ready for events.
  auto onOK = std::make_unique<OnOKListener>();
  auto onCall = std::make_unique<OnCallListener>();
  auto onNocarrier = std::make_unique<OnNOCARRIERListener>();
  auto onCmt = std::make_unique<OnCMTListener>();
  onNocarrierPtr = onNocarrier.get();
  sim900.registerListener(std::move(onOK));
  sim900.registerListener(std::move(onCall)); // register a on call listener which prints " is calling" to Serial
  sim900.registerListener(std::move(onNocarrier));
  sim900.registerListener(std::move(onCmt));

  SIM900RTC current = sim900.rtc();
  Time::storeRTC(current);
  Time::printRTC(current);

  Serial.println(F("waiting for incoming data..."));
}

void loop() {
  digitalWrite(STATUS_LED, led_state); led_state = !led_state;

  // Every 2 seconds print the message in the buffer
  if (millis() - start_time_buffer >= 2000) {
    start_time_buffer = millis();
    onNocarrierPtr->buffer.printBuffer();
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
    onNocarrierPtr->has_valid_data = false;
    daily_task_done = true;
  } else if (Time::getMillisSinceMidnight() < 23UL * Time::oneHour) {
    daily_task_done = false;
  }

  /*while(Serial1.available()) {
    Serial.write(Serial1.read());
  }*/

  SIM900_Handler_Event event = sim900.handleEvents();

  // Non-blocking serial command reader
  while (Serial.available() > 0) {
    char c = Serial.read();
    Message msg = cmdbuffer.read(c);

    // Only process messages when a full command has been entered (not INVALID)
    if (msg.type != INVALID) {
        switch (msg.type) {
            case STOREBUFFER:
                // Ensure the message isn't too long for our buffer
                if (msg.message != nullptr && strlen(msg.message) >= onNocarrierPtr->buffer.sizeOfBuffer()) {
                    Serial.print(F("error: message is too long\n"));
                } else if (msg.message != nullptr) {
                    // Safely copy the message to the global buffer
                    onNocarrierPtr->buffer.copyToBuffer(msg.message);
                    onNocarrierPtr->buffer.printBuffer();
                    if (msg.message_received_time > 0) {
                        onNocarrierPtr->message_received_time = msg.message_received_time;
                        onNocarrierPtr->has_valid_data = true;
                    }
                } // if message is null, it was an invalid storebuffer command, do nothing.
                break;
            case SENDSMS:
                if (msg.phonenumber != nullptr && msg.message != nullptr)
                    sim900.sendSMSRoutine(msg.phonenumber, msg.message);
                break;
            case EMPTY:
                // If the command was empty (e.g., just pressing Enter), print the buffer
                onNocarrierPtr->buffer.printBuffer();
                break;
            case TIME:
                // The 'time' command already prints to Serial inside CommandBuffer.
                // No action needed here.
                break;
        }
    }
  }

  delay(50); // Reduced delay to make loop more responsive
}