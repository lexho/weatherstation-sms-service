# weatherstation-sms-service
sends an sms with the current weather data

This code is written for an **Arduino Nano 33 BLE** or similar with hardware serial ports.

Send message to arduino with the `storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O` command.

Call your shield or send sms to it and it will repond with `time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O`.

Requirements:
* SIM900-GSM-Shield
* Arduino Nano 33 BLE

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