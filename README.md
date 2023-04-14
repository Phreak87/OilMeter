# OilMeter

Measure the oil consumption via the burner status LED on a ESP8266 with a TCS34725 or APDS9960 color sensor.
BOM ist just ~10 Euro for all parts. This project is under active development and NOT FULLY FINISHED yet!

## Oilmeter comes with the following features included:
- First start as AccessPoint (OilMeter, PWD: OilMeter), after saving Wifi-Settings it will connect to default Wifi.
- Nice handmade and Mobile-friendly Webinterface for configuration and status with live streaming updates via web-events.
- Reads the sensor status (Preheat, Blower start, burn, error and off) based on predefined color-ranges and calculates oil-consumption
  by the definition of L/h of your heater.
- MQTT-Connection with Homeassistant: Publishes status to a MQTT-Broker and automatically creates entities in Homeassistant.
- Update on the fly with direct Firmware Uploads (powered by ElegantOTA).
- existing Sensor adapter plates can be 3D-Printed.

## Notes

If you have problems with ESP-restarts then please consider that the burner needs a high voltage to start burning.
this high voltage can be transmitted via the wires and causes the esp to restart. to avoid this you can take a foil
(or some tabac-box) to isolate the esp and network cables for the sensor. Make sure you put the isolation material with
a extra-wire to any ground in your house.

## Pictures

![Installation](Pictures/Installation.jpg)
![Installation](Pictures/Adapter_Plate.png)
![Installation](Pictures/Webinterface.png)

# Connection diagram (Will follow)

| ESP   | GYP | TCS |
| ----- | --- | --- |
| 3V3   | VCC | VIN |
| GND   | GND | GND |
| D6    | INT | LED |
| D2    | SDA | SDA |
| D1    | SCL | SCL |

#Todo

- Multicolor LED for sensor status and system messages
- feedback if setting was saved.
- Not try to send if MQTT Offline
- auto adaptation of the correct factor value on fill-up (needs to be between 2 times full)
- Informations about last update in the webinterface to make sure ESP is alive.
- Burning interval with decimal places (just 100 minutes is too less - like 100.01)
- Burn settings – calculation factor from L to kW Heating power for Homeassistant Energy Dashboard.
- Don't save actual tank volume in burn settings (exlude all changing files in usage.json)
  - actual tank volume in liter and percent - maybe with animated svg-picture.
  - Full burning runtime
  - Full consumption in liter and kWh
  - last burn runtime
  - last interval time
  - daily times/consumption.
  - last consumption in liter and kWh
- Show system runtime and reboot cause in webinterface.
- compress webpages and js with compressors and gzip before uploading.
- webpages based on local bootstrap.
- MQTT-Homeassistant optimizations and more values.
  

