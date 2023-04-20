# OilMeter

Measure the oil consumption via the burner status LED on a ESP8266 with a TCS34725 or APDS9960 color sensor.
BOM is just ~10 Euro for all parts. 

## Oilmeter comes with the following features included:
- First start as AccessPoint (OilMeter, PWD: OilMeter), after saving Wifi-Settings it will connect to default Wifi.
- Nice handmade and Mobile-friendly Webinterface for configuration and status with live streaming updates via web-events.
- Reads the sensor status (Preheat, Blower start, burn, error and off) based on predefined color-ranges and calculates oil-consumption
  by the definition of L/h of your heater.
- MQTT-Connection with Homeassistant: Publishes status to a MQTT-Broker and automatically creates entities in Homeassistant.
- Updates on the fly with direct Firmware Uploads (powered by ElegantOTA).
- existing Sensor adapter plates can be 3D-Printed.

## Notes

If you have problems with ESP-restarts please consider that the burner needs a high voltage to start burning.
This high voltage can be transmitted via the wires and causes the ESP to restart. To avoid this you can take a foil
(or some tabac-box) to isolate the esp and network cables to isolate the sensor wires. Make sure you put the isolation material with
a extra-wire to any ground in your house.

To make good measures you need to know the actual tank volume. You can do this by estimate based on the height of the oil in the tank
or you start from scratch if the tank is filled up to its maximum. After the first refill you can adapt your values based on whats 
really used vs. whats refilled.

## Pictures

![Installation](Pictures/Installation.jpg)
![3D Model](Pictures/Adapter_Plate.png)
![Webinterface](Pictures/Webinterface.png)
![Homeassistant](Pictures/HassIO.png)

# Connection diagram

| ESP   | GYP | TCS |
| ----- | --- | --- |
| 3V3   | VCC | VIN |
| GND   | GND | GND |
| D6    | INT | LED |
| D2    | SDA | SDA |
| D1    | SCL | SCL |

#Todo

- Integration of HC-SR04 Distance Sensor for Level Measurement.
- Multicolor LED for sensor status and system messages
- better feedback if setting was saved.
- auto adaptation of the correct factor value on fill-up (needs to be between 2 times full)
- animated svg-picture of the tank volume in percent
- Show system up time in webinterface.
- compress webpages and js with compressors and gzip before uploading.
- webpages based on local bootstrap.
- MQTT-Homeassistant optimizations and more values.
  

