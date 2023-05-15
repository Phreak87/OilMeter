# OilMeter

Measure fluid consumption and levels on a ESP8266 via different Sensors:
- TCS34725 or APDS9960 color sensor (Consumption via status-LED and Time)
- HC-SR04/JSN-SR04T distance-sensor (just leveling).
- VL53L0X Laser Distance            (just leveling, not use for clear Water or add a swimmer)
- Trigger Sensor                    (Trigger Sensors like IR, Button, Motion detector, sound or proximity switch (time based and trigger consumption)

## Oilmeter comes with the following features included:
- First start as AccessPoint (OilMeter, PWD: OilMeter), after saving Wifi-Settings it will connect to default Wifi.
- Nice handmade and Mobile-friendly Webinterface for configuration and status with live streaming updates via web-events.
- Reads the sensor status (Preheat, Blower start, burn, error and off) based on predefined color-ranges and calculates oil-consumption
  by the definition of L/h of your heater or calculates the level based on laser/sound measuring on water tanks (konical, circular and rectangular).
- MQTT-Connection with Homeassistant: Publishes status to a MQTT-Broker and automatically creates entities in Homeassistant.
- Updates on the fly with direct Firmware Uploads (powered by ElegantOTA).
- existing Sensor adapter plate for burner can be 3D-Printed.

## Flashing
Please install Visual Studio code and inside visual studio code the PlatformIO Workbench.
Load the project folder and adapt your Com-Port in Platform.ini. Click Build and Upload.
for updates you can use the OTA-Feature and the .bin files included in this repository.
More installation Methods will follow (please help how to setup Chrome-Online Firmware flasher).

## Notes

If you have problems with ESP-restarts please consider that the burner needs a high voltage to start burning.
(in the webinterface you can see the reboot-cause. 'exception' can be a hint for such a high-voltage reset).
This high voltage can be transmitted via the wires and causes the ESP to restart. To avoid restarts you should
use isolated wires for the power-switch and sensor cabling and a separate isolation (e.g. Tabac-Box) to shim the esp
itself. make sure the isolation is connected to some ground of your house. Also make sure you use a stable power-supply.

To make good measures for your oil tank you need to know the actual tank volume. You can do this by estimate based on the height of the oil in the tank
or you start from scratch if the tank is filled up to its maximum. After the first refill you can adapt your values based on whats 
really used vs. whats refilled via the adaption factor in the webinterface.

For the Level Mesurements via Laser or Sound distance measuring calculate the surface like:
(if konical then do seperate calculations for top and bottom, if not use same surface for both).
Circular:     diameter * 3,14
Quadratic:    diameter 
Rectangular:  (h * w) / 2

## Pictures

![Installation](Pictures/Installation.jpg)
![Rainwater](Pictures/RainWater.jpeg)
![3D Model](Pictures/Adapter_Plate.png)
![Webinterface](Pictures/Webinterface.png)
![Homeassistant](Pictures/HassIO.png)

# Connection diagram (3.3 or 5V based on your Sensor)

| ESP   | GYP | TCS | HC/JSN | VL52 | TRIG |
| ----- | --- | --- | ------ | ---- |----- |
| 3V3/5V| VCC | VIN | VCC    | VIN  | VIN  |
| GND   | GND | GND | GND    | GND  | GND  |
| D6    | INT | LED | ------ | ---- | ---- | 
| D2    | SDA | SDA | ------ | SDA  | ---- |
| D1    | SCL | SCL | ------ | SCL  | INT  |
| D7    |     |     | TRIG   | ---- | ---- |
| D8    |     |     | ECHO   | ---- | ---- |

#Todo

- Multicolor LED for sensor status and system messages
- auto adaptation of the correct factor value on fill-up (needs to be between 2 times full)
- animated svg-picture of the tank volume in percent
- compress webpages and js with compressors and gzip before uploading.
- webpages based on local bootstrap.
- Include SKU237545 Pressure Sensor
- Include HK1100C Pressure Sensor
- Include HX711 weight Sensor
- Include Liter per Percent heigth measurements.
