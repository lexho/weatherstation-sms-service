#include <sim900.h>
#include "time.h"
#include "messagebuffer.h"
#include "commandbuffer.h"

bool led_state = LOW;

SIM900 sim900(Serial1);

int event_status_old = 0;

unsigned long lastRingMessageTime = 0;

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
}

void loop() {
  digitalWrite(STATUS_LED, led_state); led_state = !led_state;

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
  }
  if (event.status == SIM900_CMT) {}
  if (event.status == SIM900_NOCARRIER) {
    if(sim900.isCalling()) {
      sim900.calling = false;
    }
  }

  delay(50); // Reduced delay to make loop more responsive
}