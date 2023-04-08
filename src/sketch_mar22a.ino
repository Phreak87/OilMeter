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

#include "Index.html"   // Startpage
#include "About.html"   // Über-Seite
#include "Wifi.html"    // WLAN-Einstellungen
#include "MQTT.html"    // MQTT-Einstellungen
#include "Default.html" // Übersichtsseite
#include "Sensor.html"  // Sensoreinstellungen
#include "Brenner.html" // Brennereinstellungen
#include "Style.css"    // Style für alle Seiten

// Use this values to overwrite the Wifi-Settings with 
// this hardcoded values. (fault case)
const bool WifiRestore = false;
const char* WIFISSID = "WLAN";
const char* WIFIPASS = "PASSWORD";

String LastRGB = "255,255,255"; // 255,... ist weiß und Fehler beim Sensor.
int LastStat = 0;
ulong StartMs = 0;
float ConsumL = 0;
ulong LastMS = 0;   // Backup-Strategie für Werte senden.
bool APMode = true; // Wenn AP-Mode dann MQTT ignorieren

bool SensReady = false; // Wenn Sensor init OK
bool MqttReady = false; // Wenn MQTT init OK

const byte pinLED = D6;
const char* WIFIFILENAME = "Wifi.json";
const char* MQTTFILENAME = "Mqtt.json";
const char* SENSFILENAME = "Sens.json";
const char* BURNFILENAME = "Burn.json";
DynamicJsonDocument WIFISETT(128); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument SENSSETT(128); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument MQTTSETT(256); // Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)
DynamicJsonDocument BURNSETT(1024);// Muss größer sein als tatsächlicher Verbrauch (Sonst werden werte mit null geschrieben)

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
espMqttClientAsync mqttClient;
AsyncEventSource events("/events"); 
AsyncWebServer server(80);

void setup(void) {
  Serial.begin(115200);
  Serial.println("");Serial.println(""); // Leerzeilen um vom Bootcode abzusetzen
  pinMode (pinLED, OUTPUT);

  InitLittleFS (); 

  bool LoadedWifi = LoadFile (WIFIFILENAME, WIFISETT); if(WifiRestore == true){DefaultJsonWifi(); SaveFile(WIFIFILENAME, WIFISETT);};
  bool LoadedMqtt = LoadFile (MQTTFILENAME, MQTTSETT); if(LoadedMqtt == false){DefaultJsonMqtt(); SaveFile(MQTTFILENAME, MQTTSETT);};
  bool LoadedSens = LoadFile (SENSFILENAME, SENSSETT); if(LoadedSens == false){DefaultJsonSens(); SaveFile(SENSFILENAME, SENSSETT);};
  bool LoadedBurn = LoadFile (BURNFILENAME, BURNSETT); if(LoadedBurn == false){DefaultJsonBurn(); SaveFile(BURNFILENAME, BURNSETT);};

  if (LoadedWifi){  // Wenn Wifi.json vorhanden
    Serial.println    ("Connect to saved AP)");
    bool connected = InitWifiSTA();    // Verbinde zu Router
    if (connected == true){
      APMode = false;
    } else {
      Serial.println    ("Emulate an AP ...");
      InitWifiAP ();    // Eigener Accesspoint
      APMode = true;
    }
  } else {            // sonst
    Serial.println    ("Emulate an AP ...");
    InitWifiAP ();    // Eigener Accesspoint
    APMode = true;
  }

// MQTT nur im Client-Modus erlaubt!.
  if (APMode == false){InitMQTT();}
  // InitTCS34725();   // Sensor Initialisieren.

  // HTML-Inhalte (Flash)
  server.on("/",            HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", IndexHTML); });
  server.on("/index.html",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", IndexHTML); });
  server.on("/MQTT.html",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", MqttHTML); });
  server.on("/Sensor.html", HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", SensorHTML); });
  server.on("/Brenner.html",HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", BrennerHTML); });
  server.on("/Default.html",HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", DefaultHTML); });
  server.on("/style.css",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", StyleCSS); });
  server.on("/about.html",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", AboutHTML); });
  server.on("/Wifi.html",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", WifiHTML); });
  
  server.on("/Burn.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, BURNFILENAME, "text/html");});
  server.on("/Wifi.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, WIFIFILENAME, "text/html");});
  server.on("/Mqtt.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, MQTTFILENAME, "text/html");});
  server.on("/Sens.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, SENSFILENAME, "text/html");});

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
        
        if (p -> name() == "HOST") {MQTTSETT["HOST"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "USER") {MQTTSETT["USER"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "PASW") {MQTTSETT["PASS"]  = p-> value(); MqttUpdated = true;}
        if (p -> name() == "HASS") {MQTTSETT["HASS"]  = p-> value(); MqttUpdated = true;}
        
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
  mqttClient.connect();
}
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  MqttReady = true;
  Serial.println(sessionPresent);
  PublishHASS();
}

void loop(void) {
 
// Forcing alle 2 Sekunden für das Webinterface 
// und Entlastung von MQTT bei gleichbleibenden Werten.
bool ForcedSend = false;
if (millis() - LastMS >= 2000) {
  ForcedSend = true;
  LastMS = millis ();
}

if (SensReady == true){
  // Get values and convert to int / RGB-String
  float r, g, b,  l;
  tcs.getRGB (&r, &g, &b);
  l = tcs.calculateLux(r, g, b);
  int R = (int)r;  int G = (int)g;  int B = (int)b;
  String RGB = String(R);RGB.concat(",");RGB.concat(G);RGB.concat(",");RGB.concat(B);

  if (RGB != LastRGB || ForcedSend){
    events.send(RGB.c_str(), "RGB");  
    mqttClient.publish("oilmeter/sensor/0/light_r", 0, true, String(R).c_str());
    mqttClient.publish("oilmeter/sensor/0/light_g", 0, true, String(G).c_str());
    mqttClient.publish("oilmeter/sensor/0/light_b", 0, true, String(B).c_str());
    mqttClient.publish("oilmeter/sensor/0/light_l", 0, true, String(l).c_str());
    mqttClient.loop(); delay(50);yield();
  }

  int S = BrennerStatus(R,G,B);
  Serial.print("Status(ermittelt):");
  Serial.println(S);

  if (S == 3 && LastStat != 3){StartMs = millis();}  // Start des Brennvorgangs
  if (S != 3 && LastStat == 3){
    ulong BurnTime = (millis() - StartMs) / 1000;
    Serial.print ("Gebrannt: "); 
    Serial.print (BurnTime);
    Serial.println ("Sekunden");
    events.send(String(BurnTime).c_str(), "Burned"); // Brennzeit Sekunden
    ConsumL += (BurnTime / 60 / 60) * BURNSETT["L_H"].as<float>() * BURNSETT["COR"].as<float>();
  }

  if (S != LastStat || ForcedSend){
    events.send(String(S).c_str(), "Status");
    events.send(String(BURNSETT["L_H"].as<float>()).c_str(), "L_H");
    events.send(String(BURNSETT["COR"].as<float>()).c_str(), "COR");
    events.send(String(246 / 60 / 60).c_str(), "COR");
    events.send(String(ConsumL).c_str(), "ConsumeL");
    mqttClient.publish("oilmeter/Consumption_l", 0, true, String(ConsumL).c_str());
  }

  LastRGB = RGB;  // Set last Sensor Status
  LastStat = S;   // Set last Status
} else {
  if (ForcedSend){
    events.send("Sensor Error", "RGB");  
  }
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
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);

  mqttClient.setServer(MQTTSETT["HOST"], MQTTSETT["PASS"]);
  mqttClient.setCredentials(MQTTSETT["USER"], MQTTSETT["PASS"]).setClientId("OilMeter");
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}
void InitTCS34725 (){
  // if (SENSSETT["GAIN"].as<int>() == 1)  {tcs.setGain (TCS34725_GAIN_1X);}
  // if (SENSSETT["GAIN"].as<int>() == 4)  {tcs.setGain (TCS34725_GAIN_4X);}
  // if (SENSSETT["GAIN"].as<int>() == 16) {tcs.setGain (TCS34725_GAIN_16X);}
  // if (SENSSETT["GAIN"].as<int>() == 60) {tcs.setGain (TCS34725_GAIN_60X);}
  // if (SENSSETT["INTEG"].as<float>() == 2.4) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_2_4MS);}
  // if (SENSSETT["INTEG"].as<float>() == 24) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_24MS);}
  // if (SENSSETT["INTEG"].as<float>() == 50) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_50MS);}
  // if (SENSSETT["INTEG"].as<float>() == 101) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_101MS);}
  // if (SENSSETT["INTEG"].as<float>() == 154) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_154MS);}
  // if (SENSSETT["INTEG"].as<float>() == 700) {tcs.setIntegrationTime ( TCS34725_INTEGRATIONTIME_614MS);}
  if (tcs.begin()) {
    Serial.println("Found TCS34725 sensor");

    if (SENSSETT["LIGHT"] == false) {
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
  MQTTSETT["HOST"] = IPAddress (192,168,7,2);
  MQTTSETT["USER"] = "USER"; 
  MQTTSETT["PASS"] = "PASS"; 
  MQTTSETT["HASS"] = true;
}
void DefaultJsonSens (){
  SENSSETT["INTEG"] = 50;
  SENSSETT["GAIN"] =  4; 
  SENSSETT["LIGHT"] = false; 
}

void PublishHASS (){
if (MQTTSETT["HASS"] == true){
  const char origArray[][2][20]= 
  {
      {"sensor/0/Color_R","Color_R"}, 
      {"sensor/0/Color_G","Color_G"}, 
      {"sensor/0/Color_B","Color_B"}, 
      {"sensor/0/LUX","LUX"}, 
      {"wifi/ip","ip"}, 
      {"wifi/signal","signal"}, 
      {"consume/Ges_L","Ges_Liter"}, 
      {"consume/Ges_T","Ges_Time"}
  };
  // {
  //   "dev": {
  //     "name": "oilmeter",
  //     "ids": "oilmeter_1",
  //     "cu": "http://192.168.7.107",
  //     "mf": "oilmeter",
  //     "mdl": "tcs34725",
  //     "sw": "v001"
  //   },
  //   "name": "Color_R",
  //   "stat_t": "oilmeter/sensor/0/light_r",
  //   "uniq_id": "oilmeter_sensor0_r",
  //   "unit_of_meas": "L",
  //   "exp_aft": 15,
  //   "dev_cla": "water",
  //   "stat_cla": "measurement"
  // }
  // for (int i = 0; i < sizeof(origArray); i++){
  //   DynamicJsonDocument HASSETT(1024); 
  //   HASSETT["Name"]          = origArray[i][1];
  //   HASSETT["stat_t"]        = origArray[i][0];
  //   HASSETT["uniq_id"]       = origArray[i][1];
  //   HASSETT["unit_of_meas"]  = "L";
  //   HASSETT["exp_aft"]       = 15;
  //   HASSETT["dev_cla"]       = "water";
  //   HASSETT["stat_cla"]      = "measurement";
  //   HASSETT["dev"]["name"]   = "Color_R";
  //   HASSETT["dev"]["ids"]    = "Color_R";
  //   HASSETT["dev"]["cu"]     = "Color_R"; // IP
  //   HASSETT["dev"]["mf"]     = "OilMeter";
  //   HASSETT["dev"]["mdl"]    = "TCS34725";
  //   HASSETT["dev"]["sw"]     = "v001";
  // }

  mqttClient.publish("homeassistant/sensor/oilmeter/color_r/config",0,true, "{\"name\": \"Color_R\",\"stat_t\":\"oilmeter/sensor/0/light_r\",  \"uniq_id\": \"oilmeter_sensor0_r\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
  mqttClient.publish("homeassistant/sensor/oilmeter/color_g/config",0,true, "{\"name\": \"Color_G\",\"stat_t\":\"oilmeter/sensor/0/light_g\",  \"uniq_id\": \"oilmeter_sensor0_g\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
  mqttClient.publish("homeassistant/sensor/oilmeter/color_b/config",0,true, "{\"name\": \"Color_B\",\"stat_t\":\"oilmeter/sensor/0/light_b\",  \"uniq_id\": \"oilmeter_sensor0_b\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
  mqttClient.publish("homeassistant/sensor/oilmeter/color_l/config",0,true, "{\"name\": \"LUX\",\"stat_t\":\"oilmeter/sensor/0/light_l\",  \"uniq_id\": \"oilmeter_sensor0_l\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
  mqttClient.publish("homeassistant/sensor/oilmeter/color_l/config",0,true, "{\"name\": \"Consumption\",\"stat_t\":\"oilmeter/Consumption_l\",  \"uniq_id\": \"oilmeter_Consumption_l\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
}
}