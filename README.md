# weatherstation-sms-service
sends an sms with the current weather data

This code is written for an **Arduino Nano 33 BLE** or similar with hardware serial ports.

Send message to arduino with the `storebuffer time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O` command.

Call your shield or send sms to it and it will repond with `time: 21:09:09, temp: 39.5oC, pressure: 1278.9hPa, tendency: rising, windspeed: 271.7km/h, winddir: O`.

Requirements:
* SIM900-GSM-Shield
* Arduino Nano 33 BLE