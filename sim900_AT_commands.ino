void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  while(!Serial);
  while(!Serial1);
  Serial.println(F("Serial passthrough ready."));
  Serial.println(F("Type AT commands to send to the SIM900 module."));
}

void loop() {
  if(Serial1.available() > 0) {
    while(Serial1.available() > 0) {
      Serial.write(Serial1.read());
    }
  }

  if(Serial.available() > 0) {
    while(Serial.available() > 0) {
      Serial1.write(Serial.read());
    }
  }
  delay(100);
}
