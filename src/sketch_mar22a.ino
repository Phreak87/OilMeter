#include <SPI.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJSON.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <espMqttClientAsync.h>
#include <Adafruit_TCS34725.h>
#include <SparkFun_APDS9960.h>

#include "Index.html"   // Startpage
#include "About.html"   // Über-Seite
#include "Wifi.html"    // WLAN-Einstellungen
#include "MQTT.html"    // MQTT-Einstellungen
#include "Default.html" // Übersichtsseite
#include "Sensor.html"  // Sensoreinstellungen
#include "Brenner.html" // Brennereinstellungen
#include "Style.css"    // Style für alle Seiten
#include "Menu.css"    // Style für alle Seiten

// Use this values to overwrite the Wifi-Settings with 
// this hardcoded values. (fault case)
const bool WifiRestore = false;
const char* WIFISSID = "WLAN";
const char* WIFIPASS = "PASSWORD";
int numberOfNetworks = 0;

String LastRGB = "255,255,255"; // 255,... ist weiß und Fehler beim Sensor.
int LastStat = 0;
double ConsumL = 0;

ulong AllXSec = millis();   // Messung Zeitabstand Brennvorgang
ulong LastAllXSec = 0;      // Letzter Messwert

ulong BurnMs = 0;           // Messung länge Brennvorgang in ms
double LastBurnS = 0;       // Letzte Anzahl an Sekunden Brennvorgang
double LastBurnH = 0;       // Letzte Anzahl an Stunden Brennvorgang
double LastBurnL = 0;       // Letzte Anzahl an Liter Brennvorgang

ulong Timer2s = millis();   // Backup-Strategie für Werte senden (ForcedSend).
ulong Timer60s = millis();  // Backup-Strategie für Werte senden (ForcedSend).

bool APMode = true;         // Wenn AP-Mode dann MQTT ignorieren
bool TCS = true;            // Wenn TCS34725 verwendet wurde, ansonsten APDS9960

bool SensReady = false; // Wenn Sensor init OK
bool MqttReady = false; // Wenn MQTT init OK

const byte pinLED = D6;
const char* WIFIFILENAME = "Wifi.json";
const char* MQTTFILENAME = "Mqtt.json";
const char* SENSFILENAME = "Sens.json";
const char* BURNFILENAME = "Burn.json";
const char* USAGEFILENAME = "Usage.json";
DynamicJsonDocument WIFISETT(128); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument SENSSETT(128); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument MQTTSETT(256); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument BURNSETT(1024);// Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument USAGE(512);    // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)

Adafruit_TCS34725 tcs = Adafruit_TCS34725();
SparkFun_APDS9960 apds = SparkFun_APDS9960 ();
espMqttClientAsync mqttClient;
AsyncEventSource events("/events"); 
AsyncWebServer server(80);

const char origArray[][7][60]= 
{
  // HASS-NODE                                             NAME              STATE-TOPIC                  UNIQUE-ID                 MEAS-Unit       DEV-CLA  STATE-CLASS
    {"homeassistant/sensor/oilmeter/color_r/config",       "Color_R",        "oilmeter/sensor/0/R",       "OilMeter_Sensor0_R",     "brightness",   "water", "measurement"},
    {"homeassistant/sensor/oilmeter/color_g/config",       "Color_G",        "oilmeter/sensor/0/G",       "OilMeter_Sensor0_G",     "brightness",   "water", "measurement"}, 
    {"homeassistant/sensor/oilmeter/color_b/config",       "Color_B",        "oilmeter/sensor/0/B",       "OilMeter_Sensor0_B",     "brightness",   "water", "measurement"}, 
    {"homeassistant/sensor/oilmeter/color_l/config",       "LUX",            "oilmeter/sensor/0/L",       "OilMeter_Sensor0_L",     "brightness",   "water", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastWTime/config",     "LastWTime",      "oilmeter/Burning_Interval", "OilMeter_LastWTime",     "s",            "water", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastBTime/config",     "LastBTime",      "oilmeter/Burning_Time",     "OilMeter_LastBTime",     "min",          "water", "measurement"}, 
    {"homeassistant/sensor/oilmeter/consumption_l/config", "consumption_l",  "oilmeter/Consumption_l",    "OilMeter_Consumption_L", "L",            "water", "total_increasing"}
};

void setup(void) {
  Serial.begin(115200);                   // Serielle Konsole aktivieren (115200)
  Serial.println("");Serial.println("");  // Leerzeilen um vom Bootcode abzusetzen
  pinMode (pinLED, OUTPUT);               // LED-Pin des Sensors.

  // Disable the Watchdog (Debugging)
  // 0 -> normal startup by power on
  // 1 -> hardware watch dog reset
  // 2 -> software watch dog reset (From an exception)
  // 3 -> software watch dog reset system_restart (Possibly unfed watchdog got angry)
  // 4 -> soft restart (Possibly with a restart command)
  // 5 -> wake up from deep-sleep
  // ESP.wdtDisable();
  // *((volatile uint32_t*) 0x60000900) &= ~(1); // Hardware WDT OFF

  InitLittleFS ();                        // Im LittleFS stehen die WLAN- und andere daten.
  numberOfNetworks = WiFi.scanNetworks(); // Bevor zu einem WLAN verbunden wird, sonst Absturz!

  bool LoadedWifi = LoadFile (WIFIFILENAME, WIFISETT); if(WifiRestore == true) {DefaultJsonWifi();  SaveFile(WIFIFILENAME, WIFISETT);};
  bool LoadedMqtt = LoadFile (MQTTFILENAME, MQTTSETT); if(LoadedMqtt == false) {DefaultJsonMqtt();  SaveFile(MQTTFILENAME, MQTTSETT);};
  bool LoadedSens = LoadFile (SENSFILENAME, SENSSETT); if(LoadedSens == false) {DefaultJsonSens();  SaveFile(SENSFILENAME, SENSSETT);};
  bool LoadedBurn = LoadFile (BURNFILENAME, BURNSETT); if(LoadedBurn == false) {DefaultJsonBurn();  SaveFile(BURNFILENAME, BURNSETT);};
  bool LoadedUsage = LoadFile (USAGEFILENAME, USAGE);  if(LoadedUsage == false){DefaultJsonUsage(); SaveFile(USAGEFILENAME, USAGE);};

  if (LoadedWifi){    // Wenn Wifi.json vorhanden
    Serial.println    ("Connect to saved AP)");
    bool connected = InitWifiSTA();    // Verbinde zu Router
    if (connected == true){
      APMode = false;
    } else {
      Serial.println    ("Emulate an AP ...");
      InitWifiAP ();    // Eigener Accesspoint
      APMode = true;
    }
  } else {            // sonst Standard-AP Mode
    Serial.println    ("Emulate an AP ...");
    InitWifiAP ();    // Eigener Accesspoint
    APMode = true;
  }

  Serial.println ("Wifi finished, Init MQTT");

  InitMQTT();                                                             // MQTT-Initialisieren
  if (SENSSETT["TYPE"] == "TCS34725") {InitTCS34725();}                   // entweder TCS-Sensor Initialisieren (Standard)
  if (SENSSETT["TYPE"] == "APDS9960") {InitGYP9960 ();}                   // oder ... GYP-Sensor Initialisieren
 
   Serial.println ("Sensor finished, Init Webserver");

  // HTML-Inhalte (Flash)
  server.on("/",            HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", IndexHTML); });
  server.on("/index.html",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", IndexHTML); });
  server.on("/MQTT.html",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", MqttHTML); });
  server.on("/Sensor.html", HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", SensorHTML); });
  server.on("/Brenner.html",HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", BrennerHTML); });
  server.on("/Default.html",HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", DefaultHTML); });
  server.on("/style.css",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", StyleCSS); });
  server.on("/menu.css",    HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", MenuCSS); });
  server.on("/about.html",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", AboutHTML); });
  server.on("/Wifi.html",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", WifiHTML);  });

  server.on("/Burn.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, BURNFILENAME, "text/html");});
  server.on("/Wifi.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, WIFIFILENAME, "text/html");});
  server.on("/Mqtt.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, MQTTFILENAME, "text/html");});
  server.on("/Sens.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, SENSFILENAME, "text/html");});
  server.on("/Usage.json",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, USAGEFILENAME,"text/html");});

  // Wifi-Netzwerke aus dem Start-Scan
  server.on("/NETWORKS",    HTTP_GET,   [](AsyncWebServerRequest *request){
    DynamicJsonDocument WifiNetworks(512); char json_string[256];
    for(int i =0; i<numberOfNetworks; i++){
      WifiNetworks[i]["SSID"] = WiFi.SSID(i);
      WifiNetworks[i]["RSSI"] = WiFi.RSSI(i);
    };
    serializeJson(WifiNetworks, json_string);
    request->send(200, "text/html",json_string);
  });
  // Parameter aus dem Webinterface    
  server.on("/SETTINGS",     HTTP_GET,   [](AsyncWebServerRequest *request){
    int paramsNr = request->params();
    Serial.println(paramsNr + "Received");

    bool WifiUpdated = false;
    bool MqttUpdated = false;
    bool BurnUpdated = false;
    bool SensUpdated = false;

    for(int i=0;i<paramsNr;i++){
        AsyncWebParameter* p = request->getParam(i);

        if (p -> name() == "SSID") {WIFISETT["SSID"]  = p-> value(); WifiUpdated = true;}
        if (p -> name() == "PASS") {WIFISETT["PASS"]  = p-> value(); WifiUpdated = true;}
		
        if (p -> name() == "INTEG"){SENSSETT["INTEG"] = p-> value(); SensUpdated = true;}
        if (p -> name() == "GAIN") {SENSSETT["GAIN"]  = p-> value(); SensUpdated = true;}
        if (p -> name() == "LIGHT"){SENSSETT["LIGHT"] = p-> value(); SensUpdated = true;}
        if (p -> name() == "TYPE") {SENSSETT["TYPE"]  = p-> value(); SensUpdated = true;}
        
        if (p -> name() == "HOST") {MQTTSETT["HOST"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "USER") {MQTTSETT["USER"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "PASW") {MQTTSETT["PASW"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "HASS") {MQTTSETT["HASS"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "PORT") {MQTTSETT["PORT"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "TOPC") {MQTTSETT["TOPC"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "ENAB") {MQTTSETT["ENAB"]  = p-> value(); MqttUpdated = true;}
        
        if (p -> name() == "TOL")     {BURNSETT["TOL"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "MAX")     {BURNSETT["MAX"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "ACT")     {BURNSETT["ACT"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "COR")     {BURNSETT["COR"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "L_H")     {BURNSETT["L_H"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "PREH_R")  {BURNSETT["PREHEAT"]["R"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "PREH_G")  {BURNSETT["PREHEAT"]["G"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "PREH_B")  {BURNSETT["PREHEAT"]["B"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BLOW_R")  {BURNSETT["BLOW"]["R"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BLOW_G")  {BURNSETT["BLOW"]["G"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BLOW_B")  {BURNSETT["BLOW"]["B"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BURN_R")  {BURNSETT["BURN"]["R"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BURN_G")  {BURNSETT["BURN"]["G"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "BURN_B")  {BURNSETT["BURN"]["B"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "ERRO_R")  {BURNSETT["ERROR"]["R"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "ERRO_G")  {BURNSETT["ERROR"]["G"]  = p-> value(); BurnUpdated = true;}
        if (p -> name() == "ERRO_B")  {BURNSETT["ERROR"]["B"]  = p-> value(); BurnUpdated = true;}
    }

    if (WifiUpdated == true) {SaveFile(WIFIFILENAME, WIFISETT); Serial.print("Saved Wifi-Credentials.");};
    if (MqttUpdated == true) {SaveFile(MQTTFILENAME, MQTTSETT); Serial.print("Saved Mqtt-Settings.");};
	  if (SensUpdated == true) {SaveFile(SENSFILENAME, SENSSETT); Serial.print("Saved Sensor-Settings.");};
    if (BurnUpdated == true) {SaveFile(BURNFILENAME, BURNSETT); Serial.print("Saved Burn-Settings.");};
    
    request->send(200, "text/plain", "Parameters received");
  });

  // Kommandos und Befehle
  server.on("/restart",   HTTP_GET,   [](AsyncWebServerRequest *request){ ESP.restart(); });
  server.on("/reset",     HTTP_GET,   [](AsyncWebServerRequest *request){
    // LittleFS.remove(MQTTFILENAME);
    // LittleFS.remove(WIFIFILENAME);
    LittleFS.remove(SENSFILENAME);
    LittleFS.remove(BURNFILENAME);
    request->send(200, "text/plain", "Sensor and Burn setting resetted.");
  });

  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.addHandler(&events);
  server.begin();

  Serial.println ("Server started, Loop events");
}

void SaveFile (const char* Filename, DynamicJsonDocument &Json){
  char json_string[256];
  serializeJson(Json, json_string); 
  Serial.println (json_string);
  File dataFile = LittleFS.open(Filename, "w");
  if (!dataFile) {Serial.println("Failed to open config file for writing"); return;}
  if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
  // delay(10); // Delay kills ESP with ERROR 9 because we are inside the AsyncWS-Thread!
  dataFile.close();
}
bool LoadFile (const char* Filename, DynamicJsonDocument &Json){
  File FileData = LittleFS.open(Filename, "r");
  if(!FileData){
    Serial.print("File not found: ");
    Serial.println(Filename);
    return false;
  } else {
    String FileText = FileData.readString();
    deserializeJson (Json, FileText); FileData.close();

    //char FileText[FileData.size()];
    //FileData.readBytes (FileText, FileData.size());
    //deserializeJson (Json, FileText); FileData.close();

    Serial.print("File reading success: "); 
    Serial.println(Filename);
    Serial.print("Content: "); 
    Serial.println(FileText);
    return true;
  }
}

void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
  Serial.printf("Disconnected from MQTT: %u.\n", static_cast<uint8_t>(reason));
  MqttReady = false; 
  // delay (1000); // Delay here will cause a restart.
  mqttClient.connect();
}
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  MqttReady = true;
  Serial.println(sessionPresent);
  PublishHASS();
}

void loop(void) {

// ----------------------------------------------------------
// Forcing der Werte alle 2 Sekunden für das Webinterface 
// dadurch Entlastung von MQTT bei gleichbleibenden Werten.
// ----------------------------------------------------------
bool ForcedSend = false; if (millis() - Timer2s >= 2000)    {ForcedSend = true; Timer2s  = millis ();}
bool SendHass   = false; if (millis() - Timer60s  >= 60000) {SendHass   = true; Timer60s = millis ();}

if (SendHass){PublishHASS();}

if (SensReady == true){

  // ----------------------------------------------------------
  // Sensorwerte (TCS,GYP) erfassen und in 
  // R,G,B,L als integer (0-255) normalisieren.
  // ----------------------------------------------------------
  int   R, G, B, L; // Farben (0-255), INT
  if (TCS){
    float r, g, b, l;
    tcs.getRGB (&r, &g, &b);
    l = tcs.calculateLux(r, g, b);
    R = (int)r;  G = (int)g; B = (int)b; L=(int)l;
  } else {
    uint16_t r = 0, g = 0, b = 0, l = 0; 
    if(!apds.readAmbientLight(l) ||
      !apds.readRedLight(r) ||
      !apds.readGreenLight(g) ||
      !apds.readBlueLight(b)
  ){Serial.println("Error reading light values");}
    R = (int)r ;  G = (int)g ; B = (int)b ; L=(int)l;
  }

  String RGB = String(R);RGB.concat(",");RGB.concat(G);RGB.concat(",");RGB.concat(B);

  // ----------------------------------------------------------
  // Bei Wertänderung oder alle 2 Sekunden Farbstatus senden
  // ----------------------------------------------------------
  if (RGB != LastRGB || ForcedSend){
    events.send(RGB.c_str(), "RGB");  
    mqttClient.publish("oilmeter/sensor/0/R", 0, true, String(R).c_str());
    mqttClient.publish("oilmeter/sensor/0/G", 0, true, String(G).c_str());
    mqttClient.publish("oilmeter/sensor/0/B", 0, true, String(B).c_str());
    mqttClient.publish("oilmeter/sensor/0/L", 0, true, String(L).c_str());
  }

  int S = BrennerStatus(R,G,B);

  if (S == 3 && LastStat != 3){                                                                       // Wenn brennen beginnt
    BurnMs = millis();                                                                                // Start des Brenntimers
    LastAllXSec = ((double)millis() - (double)AllXSec) / 60000.0;                                     // Brennabstand merken
    AllXSec = millis();                                                                               // Brennabstand rückstellen
  }  

  if (S != 3 && LastStat == 3){                                                                       // Wenn brennen stoppt
    LastBurnS = ((double)millis() - (double)BurnMs) / 1000.0;                                         // Brennzeit merken und auf Sek. anpassen
    LastBurnH = LastBurnS / 60.0 / 60.0;                                                              // Brennzeit in Stunden
    LastBurnL = LastBurnH * BURNSETT["L_H"].as<double>() * BURNSETT["COR"].as<double>();              // Berechnen des Verbrauchs in Liter

    Serial.println ("--------------------------------------------------------------");
    Serial.print ("Burn_s: "); Serial.print (LastBurnS,6); Serial.println (" seconds");
    Serial.print ("Burn_l: "); Serial.print (LastBurnL,6);   Serial.println (" Liter");
    Serial.println ("--------------------------------------------------------------");
    ConsumL += LastBurnL;
  }

  if (S != LastStat || ForcedSend){
    events.send(String(S).c_str(), "Status");
    events.send(String(LastBurnS,6).c_str(), "LastBTime"); 
    events.send(String(LastAllXSec,6).c_str(), "LastWTime"); 
    events.send(String(LastBurnL,6).c_str(),   "LastConsL"); 

    // mqttClient.publish("oilmeter/Heap", 0, true, String(ESP.getFreeHeap()).c_str());
    mqttClient.publish("oilmeter/Burner_Status", 0, true, String(S).c_str());
    mqttClient.publish("oilmeter/Consumption_l", 0, true, String(ConsumL,6).c_str());
    mqttClient.publish("oilmeter/Burning_Time", 0, true, String(LastBurnS,6).c_str());
    mqttClient.publish("oilmeter/Burning_Interval", 0, true, String(LastAllXSec,6).c_str());

  }

  LastRGB = RGB;      // Set last Sensor Status
  LastStat = S;       // Set last Status
  delay (250); yield();

} else {
  if (ForcedSend){events.send("Sensor Error", "RGB");}
}

}

int BrennerStatus (int &R,int &G,int &B){
  // Aus (0) als undefinierter Zustand (auch zwischen den Bereichen)
  int Status = 0; 
  // Vorheizen
  if (R > BURNSETT["PREHEAT"]["R"].as<int>() - BURNSETT["TOL"].as<int>() && 
      R < BURNSETT["PREHEAT"]["R"].as<int>() + BURNSETT["TOL"].as<int>() &&
      G > BURNSETT["PREHEAT"]["G"].as<int>() - BURNSETT["TOL"].as<int>() && 
      G < BURNSETT["PREHEAT"]["G"].as<int>() + BURNSETT["TOL"].as<int>() &&
      B > BURNSETT["PREHEAT"]["B"].as<int>() - BURNSETT["TOL"].as<int>() && 
      B < BURNSETT["PREHEAT"]["B"].as<int>() + BURNSETT["TOL"].as<int>() ){
      Status = 1; // Vorheizen
  }
  // Gebläsestart
  if (R > BURNSETT["BLOW"]["R"].as<int>() - BURNSETT["TOL"].as<int>() && 
      R < BURNSETT["BLOW"]["R"].as<int>() + BURNSETT["TOL"].as<int>() &&
      G > BURNSETT["BLOW"]["G"].as<int>() - BURNSETT["TOL"].as<int>() && 
      G < BURNSETT["BLOW"]["G"].as<int>() + BURNSETT["TOL"].as<int>() &&
      B > BURNSETT["BLOW"]["B"].as<int>() - BURNSETT["TOL"].as<int>() && 
      B < BURNSETT["BLOW"]["B"].as<int>() + BURNSETT["TOL"].as<int>() ){
      Status = 2; // Gebläsestart
  }
  // Brennen
  if (R > BURNSETT["BURN"]["R"].as<int>() - BURNSETT["TOL"].as<int>() && 
      R < BURNSETT["BURN"]["R"].as<int>() + BURNSETT["TOL"].as<int>() &&
      G > BURNSETT["BURN"]["G"].as<int>() - BURNSETT["TOL"].as<int>() && 
      G < BURNSETT["BURN"]["G"].as<int>() + BURNSETT["TOL"].as<int>() &&
      B > BURNSETT["BURN"]["B"].as<int>() - BURNSETT["TOL"].as<int>() && 
      B < BURNSETT["BURN"]["B"].as<int>() + BURNSETT["TOL"].as<int>() ){
      Status = 3; // Brennen
  }
  // Brennen
  if (R > BURNSETT["ERROR"]["R"].as<int>() - BURNSETT["TOL"].as<int>() && 
      R < BURNSETT["ERROR"]["R"].as<int>() + BURNSETT["TOL"].as<int>() &&
      G > BURNSETT["ERROR"]["G"].as<int>() - BURNSETT["TOL"].as<int>() && 
      G < BURNSETT["ERROR"]["G"].as<int>() + BURNSETT["TOL"].as<int>() &&
      B > BURNSETT["ERROR"]["B"].as<int>() - BURNSETT["TOL"].as<int>() && 
      B < BURNSETT["ERROR"]["B"].as<int>() + BURNSETT["TOL"].as<int>() ){
      Status = 4; // Störung
  }
  return Status;
}

bool InitWifiSTA (){

if (WifiRestore == true){
  Serial.println("Reset Wifi credentials");
  WIFISETT["SSID"] = WIFISSID;
  WIFISETT["PASS"] = WIFIPASS;
}

  WiFi.mode(WIFI_STA);                      // Modus Station
  WiFi.begin(WIFISETT["SSID"].as<const char*>(), WIFISETT["PASS"].as<const char*>());   // SSID, Kennwort
  WiFi.setAutoReconnect (true);
  WiFi.printDiag(Serial); 
 
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
    counter ++;
    if (counter > 20) {
      Serial.println("Wifi not connected.");
      return false;
    };
  }
 
  Serial.println("");
  Serial.print("Verbunden mit ");
  Serial.println(WIFISETT["SSID"].as<const char*>());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  return true;
}
void InitWifiAP (){
  WiFi.mode(WIFI_AP);
  WiFi.softAP("OilMeter", "OilMeter"); //  Name, Pass, Channel, Max_Conn
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}     

void InitMQTT(){
  if (APMode == false && MQTTSETT["ENAB"] == "True"){
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    IPAddress ADDR; ADDR.fromString(MQTTSETT["HOST"].as<const char*>()); 
    mqttClient.setCredentials(MQTTSETT["USER"].as<const char*>(), MQTTSETT["PASW"].as<const char*>()).setClientId("OilMeter");
    mqttClient.setServer(ADDR, MQTTSETT["PORT"].as<int>()); 
    
    Serial.println("---------------------------------");
    Serial.println("Connecting to MQTT-Broker");
    Serial.println("---------------------------------");
    Serial.println (ADDR);
    Serial.println (MQTTSETT["USER"].as<const char*>());
    Serial.println (MQTTSETT["PASW"].as<const char*>());
    Serial.println (MQTTSETT["PORT"].as<int>());
    Serial.println("---------------------------------");

    mqttClient.connect( );
  }
}
void InitGYP9960 (){
  if (apds.init() ) {
    Serial.println(F("APDS-9960 initialization complete"));
  } else {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }
  
  if (apds.enableLightSensor(false) ) {
    Serial.println(F("Light sensor is now running"));
    SensReady = true; 
  } else {
    Serial.println(F("Something went wrong during light sensor init!"));
    SensReady = false;
  }
  apds.setAmbientLightGain (GGAIN_4X);
  delay(500);
}
void InitTCS34725 (){
  if (SENSSETT["GAIN"].as<int>() == 1)  {tcs.setGain (TCS34725_GAIN_1X);}
  if (SENSSETT["GAIN"].as<int>() == 4)  {tcs.setGain (TCS34725_GAIN_4X);}
  if (SENSSETT["GAIN"].as<int>() == 16) {tcs.setGain (TCS34725_GAIN_16X);}
  if (SENSSETT["GAIN"].as<int>() == 60) {tcs.setGain (TCS34725_GAIN_60X);}
  if (SENSSETT["INTEG"].as<float>() == 2.4) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_2_4MS);}
  if (SENSSETT["INTEG"].as<float>() == 24) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_24MS);}
  if (SENSSETT["INTEG"].as<float>() == 50) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_50MS);}
  if (SENSSETT["INTEG"].as<float>() == 101) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_101MS);}
  if (SENSSETT["INTEG"].as<float>() == 154) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_154MS);}
  if (SENSSETT["INTEG"].as<float>() == 700) {tcs.setIntegrationTime ( TCS34725_INTEGRATIONTIME_614MS);}
  if (tcs.begin()) {
    Serial.println("Found TCS34725 sensor");

    if (SENSSETT["LIGHT"] == "False") {
      Serial.println("set LED off");
      digitalWrite(pinLED, LOW);
    } else {
      Serial.println("set LED on");
      digitalWrite(pinLED, HIGH);
    }

    SensReady = true;
  } else {
    Serial.println("TCS34725 init failed ... ");
    SensReady = false;
  }
}
void InitLittleFS (){
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    Serial.println("Format LittleFS"); LittleFS.format();
  } else {
    Serial.println("LittleFS Filesystem Initialized");
  }
}

void DefaultJsonWifi (){
  Serial.println ("Restore Hardcoded Wifi-Settings");
  WIFISETT["SSID"] = WIFISSID;
  WIFISETT["PASS"] = WIFIPASS;
}
void DefaultJsonBurn (){
  BURNSETT["TOL"] = 15;
  BURNSETT["L_H"] = 2.1; 
  BURNSETT["MAX"] = 4500; 
  BURNSETT["ACT"] = 3000;
  BURNSETT["COR"] = 1.0;
  BURNSETT["PREHEAT"]["R"]=50;
  BURNSETT["PREHEAT"]["G"]=50;
  BURNSETT["PREHEAT"]["B"]=50;
  BURNSETT["BLOW"]["R"]=50;
  BURNSETT["BLOW"]["G"]=50;
  BURNSETT["BLOW"]["B"]=50;
  BURNSETT["BURN"]["R"]=50;
  BURNSETT["BURN"]["G"]=50;
  BURNSETT["BURN"]["B"]=50;
  BURNSETT["ERROR"]["R"]=50;
  BURNSETT["ERROR"]["G"]=50;
  BURNSETT["ERROR"]["B"]=50;
}
void DefaultJsonMqtt (){
  MQTTSETT["HOST"] = "192.168.7.119";
  MQTTSETT["USER"] = "USER"; 
  MQTTSETT["PASW"] = ""; 
  MQTTSETT["PORT"] = 1883; 
  MQTTSETT["HASS"] = true;
  MQTTSETT["ENAB"] = false;
  MQTTSETT["TOPC"] = "OilMeter";
}
void DefaultJsonSens (){
  SENSSETT["TYPE"] = "TCS34725"; 
  SENSSETT["GAIN"] =  4; 
  SENSSETT["INTEG"] = 50;
  SENSSETT["LIGHT"] = false; 
}
void DefaultJsonUsage (){
  USAGE["LASTWAIT"] = 0;
  USAGE["LASTCONS"] = 0;
  USAGE["LASTTIME"] = 0;
}

void PublishHASS (){
if (MQTTSETT["HASS"] == "True"){

// mqttClient.publish("homeassistant/sensor/oilmeter/color_l/config",0,true, "{\"name\": \"Consumption\",\"stat_t\":\"oilmeter/Consumption_l\",  \"uniq_id\": \"oilmeter_Consumption_l\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");

  // https://www.home-assistant.io/integrations/mqtt/

    char json_string[512];
    DynamicJsonDocument HASSETT(1024);
    String URL = "http://" + WiFi.localIP().toString();
    int size = sizeof (origArray) / sizeof (origArray[0]);

    HASSETT["dev"]["name"]   = "oilmeter";
    HASSETT["dev"]["ids"]    = "oilmeter_1";
    HASSETT["dev"]["cu"]     =  URL; // IP
    HASSETT["dev"]["mf"]     = "Phreak87";
    HASSETT["dev"]["mdl"]    = "TCS34725";
    HASSETT["dev"]["sw"]     = "V001";
    for (int i = 0; i < size; i++){
      Serial.println(i);
      HASSETT["name"]          = origArray[i][1];
      HASSETT["stat_t"]        = origArray[i][2];
      HASSETT["uniq_id"]       = origArray[i][3];
      HASSETT["unit_of_meas"]  = origArray[i][4];
      HASSETT["exp_aft"]       = 15;
      HASSETT["dev_cla"]       = origArray[i][5];
      HASSETT["stat_cla"]      = origArray[i][6];
      serializeJson (HASSETT,json_string);
      mqttClient.publish (origArray[i][0],0,true,json_string);
    }

    // mqttClient.loop ();
    ESP.wdtFeed();
}
}