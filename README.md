# weatherstation-sms-service
![arduino_sms_service](https://github.com/user-attachments/assets/b20ded79-459d-4469-863f-e0c7c8b70475)

It ***sends an sms*** with the current weather data. This code has been tested on an **Arduino Nano 33 BLE** and will run any similar hardware with ***hardware serial ports***.

***Send a message*** to Arduino with the 
```
storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O
```
 command.
Use the ***serial port*** to do this.
On linux shell type:  
```
echo "storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa,..." > /dev/ttyACM0
```
The message will be buffered and it will be available via sms. After a short period of time the message will be invalidated.

***Call*** your shield or ***send an sms*** to it and the sms service will repond with 
```
time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O
```
or any other message you store in the buffer.

### Wiring and Pinout
| Conenctor | Pin | Conenctor        | Pin |
| --------- | --- | ---------------- | --- | 
| RST       | 8   | STATUS_LED       | 12  |
| PWRKEY    | 9   | STATUS_LED_ERROR | 11  |
|           |     | SIGNAL_LED1      | 4   |
|           |     | SIGNAL_LED2      | 3   |
|           |     | SIGNAL_LED3      | 2   |

I used the HardwareSerials on both Arduino and SIM900 shield.
| Nano 33 BLE | SIM900 | Pin |
| ----------- | ------ | ----|
| RX          | TX     | 1   |
| TX          | RX     | 2   |
| 8           | RST    | 7   |
| 9           | PWRKEY | 10  |
| GND         | GND    | 5   |
