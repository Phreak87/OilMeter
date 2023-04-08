# OilMeter

Measure the oil consumption via the burner status LED on a ESP8266 with a TCS34725 color sensor.
BOM ist just ~10 Euro for all parts. This project is under active development and NOT FULLY FINISHED yet!
since my TCS-Sensor is not working atm. i cannot develop further details on this project until the new one
is received. feel free to make changes and do further development.
Please note that this version is just the start of dev, is not finished and code is not well structured.

## This is just a alpha version of a Oilmeter with the following features included:
- First start as AccessPoint (OilMeter, PWD: OilMeter), after saving Wifi-Settings it will connect to default Wifi.
- Nice handmade and Mobile-friendly Webinterface for configuration and status with live streaming updates via web-events.
- Reads the sensor status (Preheat, Blower start, burn, error and off) based on predefined color-ranges and calculates oil-consumption
  by the definition of L/h of your heater (pump-ressure & oil inlet diameter).
- MQTT-Connection with Homeassistant: Publishes status to a MQTT-Broker and automatically creates entities in Homeassistant.
- Update on the fly with OTA-Uploads (powered by ElegantOTA).

## Pictures


# Connection diagram (Will follow)

ESP8266 | TCS34725
V+      | V+
SDL     | SDL
SDA     | SDA
LED     | D6
