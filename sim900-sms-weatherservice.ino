#include <sim900.h>

#define STATUS_LED 12
#define STATUS_LED_ERROR 11
#define SIGNAL_LED1 4
#define SIGNAL_LED2 3
#define SIGNAL_LED3 2
bool led_state = LOW;

#define RST_PIN 8
#define PWRKEY 9

SIM900 sim900(Serial1);

unsigned long start_time_buffer = 0;
char buffer[161]; // Max SMS length + null terminator
char buffer_old[161];
unsigned long dataset_age = 0; // in seconds since midnight
char phonenumber[14]; //+XXxxxxxxxxxx 
bool sendSMS = false;
bool calling = false;
unsigned long calling_time = 4294962000UL; // near to the but not the maximum value
bool smsSent = false;
unsigned long lastRingMessageTime = 0; // Timestamp for the last "ringing" message
int event_status_old = 0;
unsigned long lastDailyTaskTime = 0; // For rollover-safe daily tasks
const unsigned long oneDay = 1000UL * 60 * 60 * 24; // Use UL to prevent int overflow during calculation
const unsigned long oneMinute = 60UL;

const byte serialCmdBufferSize = 255;
char serialCmdBuffer[serialCmdBufferSize];
byte serialCmdBufferIdx = 0;

uint8_t day;
uint8_t month;
uint8_t year;

uint8_t hour;
uint8_t minute;
uint8_t second;

void setup() {
  pinMode(STATUS_LED, OUTPUT);
  pinMode(STATUS_LED_ERROR, OUTPUT);
  pinMode(PWRKEY, OUTPUT);
  pinMode(RST_PIN, OUTPUT); 
  digitalWrite(PWRKEY, LOW); // Keep PWRKEY low initially
  digitalWrite(RST_PIN, HIGH); // Keep SIM900 out of reset state
  // led test
  digitalWrite(STATUS_LED, HIGH);
  digitalWrite(STATUS_LED_ERROR, HIGH);
  delay(2000);
  
  while(!bootstrap()) {
    delay(5000); // Wait before retrying
  }

  SIM900RTC current = sim900.rtc();
  storeRTC(current);
  printRTC(current);

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

  // Rollover-safe check to update RTC once per day
  if (millis() - lastDailyTaskTime >= oneDay) {
    lastDailyTaskTime = millis(); // Reset the timer for the next day
    SIM900RTC current = sim900.rtc();
    storeRTC(current);
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
    unsigned long secondssincemidnight = getSecondsSinceMidnight();
    if((dataset_age + oneMinute*4) > secondssincemidnight) {
      sendSMSRoutine(phonenumber, buffer);
    } else {
      sendSMSRoutine(phonenumber, "Keine aktuellen Wetterdaten verfugbar.");
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

bool bootstrap() {
  static bool reset_attempted = false;
  int signal_strength = 0; // Declare variable at the top of the scope
  String resp;
  bool simcardOK = false;
  bool signalOK = false;

  digitalWrite(STATUS_LED, LOW);
  digitalWrite(STATUS_LED_ERROR, LOW);
  // stage 0
  digitalWrite(STATUS_LED, HIGH); // stage 1
  digitalWrite(STATUS_LED, LOW);
  Serial.begin(19200);
  while(!Serial);
  Serial.println(F("--------------------------"));
  Serial.println(F("Arduino SIM900 SMS WEATHER SERVICE"));
  Serial.println(F("--------------------------"));
  Serial.println(F("[#     ] stage1: status led"));
  Serial.println(F("[##    ] stage2: serial ready")); // stage 2
  Serial1.begin(9600);
  while(!Serial1);
  Serial.println(F("[###   ] stage3: shield serial seems to be ready")); // stage 3
  delay(1000);
  // can we write to Serial1?
  Serial.println(F("can we write to Serial1?"));
  Serial1.println(F("AT"));
  Serial.println(F("wrote 'AT'-command to Serial1"));
  Serial.println(F("trying to read from Serial1"));
  delay(400);
  if(Serial1.available()) {
    String response;
    if(Serial1.available() > 0) {
        response = Serial1.readString();
        //response.trim();
    }
    //Serial.println();
    Serial.println(F("read ")); Serial.print(response.length()); Serial.println(F(" bytes"));
    // stage 4
    Serial.print(F("response: ")); Serial.println(response);
    if(response.length() > 0) Serial.println(F("got a response."));
    if(response.indexOf("ERROR") != -1) {
      digitalWrite(STATUS_LED_ERROR, HIGH);
      Serial.println(F("did receive an 'AT ERROR'"));
      return false;
    }
    if(response.indexOf("OK") != -1) {
      Serial.println(F("[####  ] stage4: shield serial is ready")); // stage 4
    } else {
      digitalWrite(STATUS_LED_ERROR, HIGH);
      Serial.println(F("Error: did not receive valid response"));
      Serial.println(F("cannot read from shield serial. power down? check power state, check wiring"));
      // reset; if test fails poweron
      goto bootstrap_fail;
    }
  } else {
    Serial.println(F("Error: No serial data available from SIM900."));
    goto bootstrap_fail;
  }

  // stage 5 handshake from library
  if(sim900.handshake()) {
    Serial.println(F("[##### ] stage5: handshaked!"));
  } else {
    Serial.println(F("Error: handshake failed."));
    goto bootstrap_fail;
  }

  // stage 6 sim cardok, signal strengthok, net status, receive calls, sms
  sim900.sendCommand("AT+CPIN?");
  resp = sim900.getResponse();
  if(resp.indexOf("+CPIN: READY") != -1) {
    simcardOK = true;
    Serial.println(F("sim card is ready"));
  } else {
    digitalWrite(STATUS_LED_ERROR, HIGH);
    Serial.println(F("sim card is not ready"));
    return false;
  }

  signal_strength = measureSignalStrength();
  signalOK = isSignalOk(signal_strength);
  if(simcardOK && signalOK) {
    Serial.println(F("[######] stage6: simcard is ready and signal is OK"));
  } else {
    digitalWrite(STATUS_LED_ERROR, HIGH);
    return false;
  }

  reset_attempted = false; // Success, so reset the flag for the next time bootstrap might fail.
  return true;

bootstrap_fail:
  digitalWrite(STATUS_LED_ERROR, HIGH);
  Serial.println(F("bootstrap failed. attempting recovery..."));
  if (!reset_attempted) {
    SIM900reset();
    reset_attempted = true;
  } else {
    SIM900powerOn();
  }
  return false;
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
        time_ptr += 6; // Move pointer past "time: "
        dataset_age = ((time_ptr[0] - '0') * 10UL + (time_ptr[1] - '0')) * 3600UL +
                      ((time_ptr[3] - '0') * 10UL + (time_ptr[4] - '0')) * 60UL +
                      ((time_ptr[6] - '0') * 10UL + (time_ptr[7] - '0'));
      }
      // Safely copy the message to the global buffer
      strncpy(buffer, message, sizeof(buffer) - 1);
      buffer[sizeof(buffer) - 1] = '\0'; // GUARANTEE null termination
    } else {
      // The command was just "storebuffer" with no data. Print current buffer.
      Serial.print("current buffer: "); Serial.print(buffer); Serial.print('\n');
    }

  } else if (strcmp(command, "time") == 0) {
    String s = getFakeHardwareClockTime();
    while(millis() % 1000 != 0); // print time at precise time intervals
    Serial.println("time: " + s);
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
      sendSMSRoutine(phonenumber, message);
    } else {
      Serial.println(F("sms could not be sent."));
    }
  }  //else
}

bool sendSMSRoutine(const char* phonenumber, const char* message) {
  const int max_retries = 2;
  for(int retries = max_retries; retries > 0; retries--) {
    delay(200);
    if (!sim900.handshake()) {
      Serial.println(F("handshake failed, retrying..."));
      continue; // Skip to the next attempt
    }

    Serial1.readString(); // Clear any lingering response from the buffer
    Serial.println(F("sending sms..."));
    Serial.print(F("phonenumber: \"")); Serial.print(phonenumber); Serial.println(F("\""));

    bool sent = false;
    if(strlen(message) > 0) {
      Serial.print(F("message: ")); Serial.println(message);
      sent = sim900.sendSMS(phonenumber, message); // Convert to String for the library function
    } else {
      Serial.println(F("no message to send."));
    }

    if (sent) {
      return true; // Success! Exit the function.
    }
  }
  return false;
}

String sendCommand(String command) {
  Serial1.println(command);
  delay(500);
  String response = String();
  while(Serial1.available()) {
    char ch = Serial1.read();
    //Serial.print(ch);
    response += ch;
  }
  return response;
}

int rssiToDbm(int rssi) {
  if (rssi == 99) {
    return 999; // Represents "not known or not detectable"
  }
  if (rssi == 0) {
    return -113;
  }
  if (rssi == 1) {
    return -111;
  }
  if (rssi == 31) {
    return -51;
  }
  if (rssi >= 2 && rssi <= 30) {
    // Linear conversion for the main range
    return -113 + (rssi * 2);
  }
  
  return 999; // Return an error code for any other value
}

int measureSignalStrength() {
  SIM900Signal signal = sim900.signal(); // 0 - 31
  int signal_rssi = signal.rssi;
  digitalWrite(SIGNAL_LED1, LOW); digitalWrite(SIGNAL_LED2, LOW); digitalWrite(SIGNAL_LED3, LOW);
  if(signal_rssi >= 0) { digitalWrite(SIGNAL_LED1, HIGH); }
  if(signal_rssi >= 11) { digitalWrite(SIGNAL_LED2, HIGH); }
  if(signal_rssi >= 21) { digitalWrite(SIGNAL_LED3, HIGH); }
  //Serial.print(signal_rssi); // -113dBm to -51dBm
  int signal_strength = rssiToDbm(signal_rssi);
  Serial.print(F("signal strength: "));
  Serial.print(signal_strength); // -113dBm to -51dBm
  Serial.println(F("dBm"));
  if (signal_rssi >= 2 && signal_rssi < 10) {
    Serial.println(F("signal strength is marginal."));
  }
  if (signal_rssi >= 10 && signal_rssi <= 30) {
    Serial.println(F("signal strength is OK."));
  }
  if(signal_rssi == 0 || signal_rssi == 1 || signal_rssi == 31) {
    Serial.println(F("bad signal"));
  }
  return signal_rssi;
}

bool isSignalOk(int signal_strength) {
  if(signal_strength >= 10 && signal_strength <= 30) return true;
  else return false;
}

void storeRTC(SIM900RTC rtc) {
  day = rtc.day;
  month = rtc.month;
  year = rtc.year;

  hour = rtc.hour;
  minute = rtc.minute;
  second = rtc.second;
  String datetime = String("store datetime: ");
  datetime.concat("store datetime: ");
  datetime.concat(day);
  datetime.concat(".");
  datetime.concat(month);
  datetime.concat(".");
  datetime.concat(year);
  datetime.concat(" ");
  datetime.concat(hour);
  datetime.concat(":");
  datetime.concat(minute);
  datetime.concat(":");
  datetime.concat(second);
  Serial.println(datetime);
}

// hour:minute:seconds
String getFakeHardwareClockTime() {
  // seconds since midnight
  unsigned long sum = 0;
  sum += (unsigned long)hour * 3600UL;
  sum += (unsigned long)minute*60UL;
  sum += (unsigned long)second;
  sum += millis()/1000UL;

  /*Serial.print("sum: ");
  Serial.println(sum);*/
  
  int s = sum % 60;
  unsigned long total_minutes = sum / 60;
  int min = total_minutes % 60;
  unsigned long total_hours = total_minutes / 60;
  int h = total_hours % 24;

  //Serial.print("fakeHardwareClock time: ");
  //Serial.print((int)h); Serial.print(":"); Serial.print((int)min); Serial.print(":"); Serial.println(s);

  String timestr = String();
  timestr.concat(day>9 ? "" : "0");
  timestr.concat(day);
  timestr.concat(".");
  timestr.concat(month>9 ? "" : "0");
  timestr.concat(month);
  timestr.concat(".");
  timestr.concat(year>9 ? "" : "0");
  timestr.concat(year);
  timestr.concat(" ");
  timestr.concat(h>9 ? "" : "0");
  timestr.concat(h);
  timestr.concat(":");
  timestr.concat(min>9 ? "" : "0");
  timestr.concat(min);
  timestr.concat(":");
  timestr.concat(s>9 ? "" : "0");
  timestr.concat(s);
  return timestr;
}

// returns seconds since midnight
unsigned long getSecondsSinceMidnight() {
  unsigned long secondssincemidnight = 0;
  secondssincemidnight += (unsigned long)hour*3600UL;
  secondssincemidnight += (unsigned long)minute*60UL;
  secondssincemidnight += (unsigned long)second;
  secondssincemidnight += millis()/1000UL;
  return secondssincemidnight;
}

void SIM900reset()
{
  Serial.println(F("performing soft reset..."));
  digitalWrite(RST_PIN, LOW); // Set the pin LOW to trigger reset
  delay(100);                  // Wait briefly
  digitalWrite(RST_PIN, HIGH);  // Set the pin HIGH to release from reset
  delay(1000);                 // Wait for the module to restart
}

void SIM900powerOn()
{
  Serial.println(F("performing power cycle..."));
  digitalWrite(PWRKEY, HIGH);
  delay(1000); // Hold PWRKEY for 1 second
  digitalWrite(PWRKEY, LOW);

  Serial.println(F("waiting for sim900 to be available..."));
  delay(10000); // Wait for the module to boot

  if(sim900.isReady()) Serial.println(F("sim900 is on and ready."));
}

void printRTC(SIM900RTC datetime) {
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