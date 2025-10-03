#include <SPI.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>
#include <sim900.h>
#include <jsonlib.h>

SoftwareSerial shieldSerial(2, 3);

long start_time;
long oneSecond = 1000;
long oneMinute = oneSecond * 60;
long oneHour = oneMinute * 60;
long oneDay = oneHour * 24;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
char server[] = "10.0.0.15";    // name address for Google (using DNS)
int port = 8080;
String get = "GET /api/simple/ HTTP/1.1";
String host = "10.0.0.15";
IPAddress ip(10, 0, 0, 6);
IPAddress myDns(8, 8, 8, 8);
String phonenumber = "+XXxxxxxxxxxx";

EthernetClient client;

// Variables to measure the speed
unsigned long beginMicros, endMicros;
unsigned long byteCount = 0;

long interval = oneMinute*30;
bool printWebData = true;  // set to false for better speed measurement
bool sendSMS = true;
bool useDHCP = false;

void setup() {
  //shieldSerial.begin(9600);
  Serial.begin(9600);
  //if(shieldSerial) Serial.println("gsm shield available");
  if(true || !shieldSerial) { SIM900power(); shieldSerial.begin(9600); } // power up gsm shield if it is down
  //while (!Serial);
  start_time = millis();

  // start the Ethernet connection:
  if(useDHCP) {
    Serial.println("Initialize Ethernet with DHCP:");
    if (Ethernet.begin(mac) == 0) {
      Serial.println("Failed to configure Ethernet using DHCP");
      // Check for Ethernet hardware present
      if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
        while (true) {
          delay(1); // do nothing, no point running without Ethernet hardware
        }
      }
      if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected.");
      }
      // try to configure using IP address instead of DHCP:
      Ethernet.begin(mac, ip, myDns);
    } else {
      Serial.print("  DHCP assigned IP ");
      Serial.println(Ethernet.localIP());
    }
  } else {
    Ethernet.begin(mac, ip, myDns);
    Serial.println("my IP address: ");
    Serial.println(Ethernet.localIP());
  }
  // give the Ethernet shield a second to initialize:
  delay(1000);
  request();
}

void loop() {
  if(millis() > start_time + interval) {
    Serial.println();
    start_time = millis();
    request();
  }
  delay(100);
}

void SIM900power()
{
  pinMode(9, OUTPUT); 
  digitalWrite(9,LOW);
  delay(1000);
  digitalWrite(9,HIGH);
  delay(2000);
  digitalWrite(9,LOW);
  delay(3000);
}

bool connect() {
  Serial.print("connecting to ");
  Serial.print(server);
  Serial.println("...");

  // if you get a connection, report back via serial:
  if (client.connect(server, port)) {
    //Serial.println(client.remoteIP());
    // Make a HTTP request:
    client.println(get);
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();
    return true;
  } else {
    // if you didn't get a connection to the server:
    Serial.println("connection failed");
    return false;
  }
  beginMicros = micros();
}

void readBuffer() {
  String text = String();
  bool store = false;

  // Use a char array to store the incoming JSON data
  const int JSON_BUFFER_SIZE = 512;
  char jsonBuffer[JSON_BUFFER_SIZE] = {0};
  int jsonIndex = 0;
  bool isBody = false; // Flag to indicate we are past the HTTP headers


  while(true) {
    // if there are incoming bytes available
    // from the server, read them and print them:
    int len = client.available();
    if (len > 0) {
      // Read a chunk of data
      byte chunkBuffer[80];
      int readLen = client.read(chunkBuffer, min(len, sizeof(chunkBuffer)));
      
      // Find the start of the JSON body (after the double newline)
      if (!isBody) {
        char* bodyStart = strstr((char*)chunkBuffer, "\r\n\r\n");
        if (bodyStart != nullptr) {
          isBody = true;
          bodyStart += 4; // Move pointer past the newlines
          int bodyLen = readLen - (bodyStart - (char*)chunkBuffer);
          if (jsonIndex + bodyLen < JSON_BUFFER_SIZE) {
            memcpy(jsonBuffer + jsonIndex, bodyStart, bodyLen);
            jsonIndex += bodyLen;
          }
        }
      } else {
        // Already in body, just append data
        if (jsonIndex + readLen < JSON_BUFFER_SIZE) {
          memcpy(jsonBuffer + jsonIndex, chunkBuffer, readLen);
          jsonIndex += readLen;
        }
      }
      byteCount = byteCount + len;
    }

    // if the server's disconnected, stop the client:
    if (!client.connected()) {
      endMicros = micros();
      client.stop();
      Serial.print("Received ");
      Serial.print(byteCount);
      Serial.print(" bytes in ");
      float seconds = (float)(endMicros - beginMicros) / 1000000.0;
      Serial.print(seconds, 4);
      float rate = (float)byteCount / seconds / 1000.0;
      Serial.print(", rate = ");
      Serial.print(rate);
      Serial.print(" kbytes/second");
      Serial.println();

      /*Serial.print("text length: ");
      Serial.println(text.length());*/

      /*//String json = String("{},{},{},{},[{\"id\":3834,\"timestamp\":1759486832072,\"date\":\"03.10.2025\",\"time\":\"12:20:32\",\"weather\":{\"temp\":{\"value\":\"23\"},\"pressure\":{\"value\":\"1277.7\"},\"tendency\":{\"value\":\"rising\"},\"windspeed\":{\"value\":\"286.2\"},\"winddir\":{\"value\":\"NO\"}}}]");
      String weather_0 = jsonIndexList(text, 0);
      String posStr = jsonExtract(weather_0, "weather");          // {"lon":-77.35,"lat":38.93}
      int id = jsonExtract(weather_0, "id").toInt();
      String time = jsonExtract(weather_0, "time");
      String tempstr = jsonExtract(posStr, "temp");
      float temp = jsonExtract(tempstr, "value").toFloat();    // 38.93
      
      Serial.print("id: ");
      Serial.println(id);
      Serial.print("time: ");
      Serial.println(time);
      Serial.print("temp: ");
      Serial.println(temp);*/

      Serial.println("--- Received JSON ---");
      Serial.println(jsonBuffer);
      Serial.println("---------------------");

      /*if (printWebData) {
        //Serial.println(text.substring(0, 400));
        //if(text.length() > 400) Serial.println(text.substring(400, text.length()));
      }*/
      
      if(sendSMS) {
        //String smstext = text.substring(text.length()-100,text.length());
        Serial.print("smstext: \"");
        Serial.print(jsonBuffer);
        Serial.println("\"");
        if(jsonBuffer[0] == '\0') return; // do not send empty sms
        SIM900 sim900(shieldSerial);
          Serial.println(
            sim900.sendSMS(phonenumber, String(jsonBuffer))
              ? "SMS sent!" : "SMS not sent."
          );
      }

      // do nothing forevermore:
      /*while (true) {
        delay(1);
      }*/
      return;
    }
  }
}

void request() {
  if(connect()) readBuffer();
}

/**
 * @brief Extracts a value from a JSON string for a given key without using a JSON library.
 * 
 * This is a lightweight parser. It's not fully compliant and has limitations:
 * - It doesn't handle escaped quotes (\") inside strings.
 * - It doesn't parse nested objects to find a key (e.g., it can find "weather" but not "temp" inside "weather").
 * - It assumes a fairly simple and well-formed JSON structure.
 * 
 * @param json The source JSON C-string to parse.
 * @param key The key whose value you want to extract.
 * @param result A character buffer to store the extracted value.
 * @param resultSize The size of the result buffer.
 * @return true if the key was found and value was extracted successfully, false otherwise.
 */
bool getJsonValue(const char* json, const char* key, char* result, size_t resultSize) {
  // 1. Construct the search pattern, e.g., "\"key\":"
  char searchKey[strlen(key) + 4]; // "key": + null terminator
  sprintf(searchKey, "\"%s\":", key);

  // 2. Find the key in the JSON string
  const char* keyPos = strstr(json, searchKey);
  if (keyPos == nullptr) {
    // Key not found
    return false;
  }

  // 3. Move the pointer to the beginning of the value
  const char* valuePos = keyPos + strlen(searchKey);

  // 4. Skip leading whitespace
  while (*valuePos == ' ' || *valuePos == '\t' || *valuePos == '\n' || *valuePos == '\r') {
    valuePos++;
  }

  // 5. Determine the type of value (string, number, or object) and extract it
  const char* valueEnd = nullptr;
  if (*valuePos == '"') {
    // Value is a string, find the closing quote
    valuePos++; // Move past the opening quote
    valueEnd = strchr(valuePos, '"');
    if (valueEnd == nullptr) {
      return false; // Malformed, no closing quote
    }
  } else if (*valuePos == '{') {
    // Value is a nested object. Find the matching closing brace.
    int braceCount = 1;
    valueEnd = valuePos + 1;
    while (*valueEnd != '\0' && braceCount > 0) {
      if (*valueEnd == '{') braceCount++;
      if (*valueEnd == '}') braceCount--;
      valueEnd++;
    }
  }
  else {
    // Value is a number or boolean. Find the next comma or closing brace.
    valueEnd = valuePos;
    while (*valueEnd != '\0' && *valueEnd != ',' && *valueEnd != '}') {
      valueEnd++;
    }
  }

  if (valueEnd == nullptr) {
    return false; // Malformed
  }

  // 6. Copy the value into the result buffer
  size_t valueLen = valueEnd - valuePos;
  if (valueLen >= resultSize) {
    // Result buffer is too small
    return false;
  }

  strncpy(result, valuePos, valueLen);
  result[valueLen] = '\0'; // Null-terminate the result

  return true;
}
