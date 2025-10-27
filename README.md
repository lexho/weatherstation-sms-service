# Weatherstation SMS Service
![arduino_sms_service](https://github.com/lexho/weatherstation-sms-service/blob/main/arduino_sms_service.jpg)

I provide a few sample Arduino-sketches:

| filename | description | difficulty
| :------------------------------------------ | :---------------------------- | :---------------|
| sim900-sms-weatherservice.ino               | responds to calls and sms | the most complex one |
| sim900-sms-weatherservice_eventlistener.ino | responds to calls and sms | the most complex one with EventListener-Class derivates |
| sim900-sms-weatherservice_is_calling.ino    |just recognizes incoming calls | the most simple |

## Prerequisites
- SIM900-GSM-Shield
- [SIM900-Library](https://github.com/lexho/SIM900)
- Arduino IDE
- Arduino Nano 33 BLE or similar with HardwareSerial

## weatherstation-sms-service
### weatherservice_eventlistener
It ***sends an sms*** with the current weather data.

***Send a message*** to arduino with the 
```
storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O
```
command.
Use the ***serial port*** to do this.
On linux shell type:  
```
stty -F /dev/ttyACM0 19200
echo "storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa,..." > /dev/ttyACM0
```
The message will be buffered and it will be available via sms.

***Call*** your shield or ***send an sms*** to it and it will repond with 
```
time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O
```
or any message you store. After a short period of time the message will be invalidated.

The code is non-blocking, it follows the **reactive programming paradigma**.
The ```handleEvents()```-function in the loop() function emits events which turns the loop() function into an event loop, which should not be interrupted for too long. ```handleEvents()``` returns status codes for any event which could occur on a SIM900. For example ***RING***, ***message received***, ***AT OK***,... You can define actions bases on these events. For example every time when someone rings your SIM900 print the message ```somenone is calling...```.
```
SIM900_Handler_Event event = sim900.handleEvents();
if(event.status == SIM900.RING) {
   Serial.println("someone is calling...");
}
```
Keep the actions short to make sure handleEvents() will execute regurlary and don't misses any event.
For best experiences use my fork of the [SIM900](https://github.com/lexho/SIM900) library.
This code has been tested on an **Arduino Nano 33 BLE** and will run any similar hardware with ***hardware serial ports***.

### Wiring and Pinout
| Conenctor | Pin | Conenctor        | Pin |
| --------- | --- | ---------------- | --- | 
| RST       | 8   | STATUS_LED       | 12  |
| PWRKEY    | 9   | STATUS_LED_ERROR | 11  |
|           |     | SIGNAL_LED1      | 4   |
|           |     | SIGNAL_LED2      | 3   |
|           |     | SIGNAL_LED3      | 2   |

I used the HardwareSerials on both Arduino and SIM900 shield.

| Nano 33 BLE | SIM900 | Pin  |
| ----------- | ------ | ---- |
| RX          | TX     | 1    |
| TX          | RX     | 2    |
| 8           | RST    | 7    |
| 9           | PWRKEY | 10   |
| GND         | GND    | 5    |
