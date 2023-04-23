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
#include "Menu.css"     // Style für Menü

// --------------------------------------------------------------------------------------
// WLAN Einstellungen beim Reboot überschreiben (nützlich für Wifi-Tests).
// WifiRestore auf true setzen um Einstellungen anzuwenden.
// --------------------------------------------------------------------------------------
const bool WifiRestore = false;
const char* WIFISSID = "WLAN";
const char* WIFIPASS = "PASSWORD";

void SaveFile (const char* Filename, DynamicJsonDocument &Json){
  char json_string[256];
  serializeJson(Json, json_string); 
  // Serial.println (json_string);
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
    // Serial.print("Content: "); 
    // Serial.println(FileText);
    return true;
  }
}
bool cBool    (const char* Input){
  if (strcmp(Input, "True") == 0 || strcmp(Input, "true") == 0 || strcmp(Input, "1") == 0 ) {return true;}
  return false;
}

struct BURNStruct {
  int TOL = 15;
  double L_H = 2.1;  // Liter pro h
  double LKW = 9.8;  // KW pro Liter
  int MAX = 4500; 
  double ACT = 3000;
  double COR = 1.0;
  int PREHEAT_R =180;
  int PREHEAT_G =45;
  int PREHEAT_B=28;
  int BLOW_R=180;
  int BLOW_G=45;
  int BLOW_B=28;
  int BURN_R=70;
  int BURN_G=115;
  int BURN_B=20;
  int ERROR_R=0;
  int ERROR_G=0;
  int ERROR_B=255;

  bool Load () {
    DynamicJsonDocument JSON_DOC(512);
    bool LoadedUsage = LoadFile ("Burn.json", JSON_DOC);  
    if(LoadedUsage == false){Save();};
    TOL = JSON_DOC["TOL"];
    L_H = JSON_DOC["L_H"]; 
    LKW = JSON_DOC["LKW"];
    MAX = JSON_DOC["MAX"]; 
    ACT = JSON_DOC["ACT"];
    COR = JSON_DOC["COR"];
    PREHEAT_R = JSON_DOC["PREHEAT_R"].as<int>();
    PREHEAT_G = JSON_DOC["PREHEAT_G"].as<int>();
    PREHEAT_B = JSON_DOC["PREHEAT_B"].as<int>();
    BLOW_R = JSON_DOC["BLOW_R"].as<int>();
    BLOW_G = JSON_DOC["BLOW_G"].as<int>();
    BLOW_B = JSON_DOC["BLOW_B"].as<int>();
    BURN_R = JSON_DOC["BURN_R"].as<int>();
    BURN_G = JSON_DOC["BURN_G"].as<int>();
    BURN_B = JSON_DOC["BURN_B"].as<int>();
    ERROR_R = JSON_DOC["ERROR_R"].as<int>();
    ERROR_G = JSON_DOC["ERROR_G"].as<int>();
    ERROR_B = JSON_DOC["ERROR_B"].as<int>();
    // Serial.print   (ERROR_R); // Letzte eingelesene Werte Debuggen
    // Serial.print   (ERROR_G); // Letzte eingelesene Werte Debuggen
    // Serial.println (ERROR_B); // Letzte eingelesene Werte Debuggen
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(300); 
    JSON_DOC["TOL"] = TOL;
    JSON_DOC["L_H"] = L_H; 
    JSON_DOC["LKW"] = LKW;  // KW per Liter
    JSON_DOC["MAX"] = MAX; 
    JSON_DOC["ACT"] = ACT;
    JSON_DOC["COR"] = COR;
    JSON_DOC["PREHEAT_R"] =PREHEAT_R;
    JSON_DOC["PREHEAT_G"] =PREHEAT_G;
    JSON_DOC["PREHEAT_B"]= PREHEAT_B;
    JSON_DOC["BLOW_R"]=BLOW_R;
    JSON_DOC["BLOW_G"]=BLOW_G;
    JSON_DOC["BLOW_B"]=BLOW_B;
    JSON_DOC["BURN_R"]=BURN_R;
    JSON_DOC["BURN_G"]=BURN_G;
    JSON_DOC["BURN_B"]=BURN_B;
    JSON_DOC["ERROR_R"]=ERROR_R;
    JSON_DOC["ERROR_G"]=ERROR_G;
    JSON_DOC["ERROR_B"]=ERROR_B;

    char json_string[300];
    serializeJson(JSON_DOC, json_string); 
    File dataFile = LittleFS.open("Burn.json", "w");
    if (!dataFile) {Serial.println("Failed to open config file for writing"); return false;}
    if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
    dataFile.close();
    return true;
  }
};
struct USAGEStruct {
  double LastWaitM  = 0.0;             // Letzter Wartezeit zwischen Starts
  double LastBurnM  = 0.0;             // Letzte Anzahl an Sekunden Brennvorgang
  double LastBurnL  = 0.0;             // Letzte Anzahl an Liter Brennvorgang
  double LastGenkW  = 0.0;             // Letzte Generierte kWh

  double GesWaitM  = 0.0;              // Gesamte Wartezeit des Brenners (incl. Vorheizen)
  double GesBurnM  = 0.0;              // Gesamte Anzahl an Sekunden Brennvorgang
  double GesBurnL  = 0.0;              // Gesamte Anzahl an Liter Brennvorgang
  double GesGenkW  = 0.0;              // Gesamte Generierte kWh

  double ActTankL  = 0.0;              // Tankinhalt aktuell in Liter

  bool Load () {
    DynamicJsonDocument JSON_DOC(256);
    bool LoadedUsage = LoadFile ("Usage.json", JSON_DOC);  
    if(LoadedUsage == false){Save();};
    LastBurnL = JSON_DOC["LastBurnL"].as<double>();
    LastWaitM = JSON_DOC["LastWaitM"].as<double>();
    LastBurnM = JSON_DOC["LastBurnM"].as<double>();
    LastGenkW = JSON_DOC["LastGenkW"].as<double>();
    GesBurnL  = JSON_DOC["GesBurnL"].as<double>();
    GesWaitM  = JSON_DOC["GesWaitM"].as<double>();
    GesBurnM  = JSON_DOC["GesBurnM"].as<double>();
    GesGenkW  = JSON_DOC["GesGenkW"].as<double>();
    ActTankL  = JSON_DOC["ActTankL"].as<double>();
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(256); 

    JSON_DOC["LastWaitM"]  = LastWaitM;
    JSON_DOC["LastBurnM"]  = LastBurnM;
    JSON_DOC["LastBurnL"]  = LastBurnL;
    JSON_DOC["LastGenkW"]  = LastGenkW;
    JSON_DOC["GesWaitM"]   = GesWaitM;
    JSON_DOC["GesBurnM"]   = GesBurnM;
    JSON_DOC["GesBurnL"]   = GesBurnL;
    JSON_DOC["GesGenkW"]   = GesGenkW;
    JSON_DOC["ActTankL"]   = ActTankL;

    char json_string[256];
    serializeJson(JSON_DOC, json_string); 
    File dataFile = LittleFS.open("Usage.json", "w");
    if (!dataFile) {Serial.println("Failed to open config file for writing"); return false;}
    if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
    dataFile.close();
    return true;
  }
};
struct WIFIStruct {
  const char* SSID  = "";
  const char* PASS  = "";

  bool Load () {
    DynamicJsonDocument JSON_DOC(64);
    bool LoadedUsage = LoadFile ("Wifi.json", JSON_DOC);  
    if(LoadedUsage == false){Save();};
    SSID = JSON_DOC["SSID"];
    PASS = JSON_DOC["PASS"];
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(64); 
    JSON_DOC["SSID"]  = SSID;
    JSON_DOC["PASS"]   = PASS;
    char json_string[64];
    serializeJson(JSON_DOC, json_string); 
    File dataFile = LittleFS.open("Wifi.json", "w");
    if (!dataFile) {Serial.println("Failed to open config file for writing"); return false;}
    if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
    dataFile.close();
    return true;
  }
};
struct SENSStruct {
  char        TYPE[16] = "TCS34725"; // oder APDS9960
  char        LIGHT[8] = "False"; 
  int         GAIN     =  4; 
  int         INTEG    = 50;

  bool Load () {
    DynamicJsonDocument JSON_DOC(128);
    bool Loaded = LoadFile ("Sens.json", JSON_DOC);  
    if(Loaded == false){Save();};
    strcpy( TYPE, JSON_DOC["TYPE"]);  
    strcpy( LIGHT, JSON_DOC["LIGHT"]);  
    GAIN = JSON_DOC["GAIN"];
    INTEG = JSON_DOC["INTEG"];
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(128); 
    JSON_DOC["TYPE"]  = TYPE;
    JSON_DOC["GAIN"]  = GAIN;
    JSON_DOC["INTEG"] = INTEG;
    JSON_DOC["LIGHT"] = LIGHT;
    char json_string[128];
    serializeJson(JSON_DOC, json_string); 
    File dataFile = LittleFS.open("Sens.json", "w");
    if (!dataFile) {Serial.println("Failed to open config file for writing"); return false;}
    if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
    dataFile.close();
    return true;
  }
};
struct MQTTStruct {
  char TOPC[16]   = "OilMeter";
  char HOST[16]   = "192.168.7.119";
  char USER[16]   = ""; 
  char PASW[16]   = ""; 
  int  PORT       = 1883; 
  bool HASS       = true;
  bool ENAB       = true;

  bool Load () {
    DynamicJsonDocument JSON_DOC(256);
    bool LoadedUsage = LoadFile ("Mqtt.json", JSON_DOC);  
    if(LoadedUsage == false){Save();};

    strlcpy (TOPC, JSON_DOC["TOPC"], sizeof(TOPC)); 
    strlcpy (HOST, JSON_DOC["HOST"], sizeof(HOST));
    strlcpy (USER, JSON_DOC["USER"], sizeof(USER));
    strlcpy (PASW, JSON_DOC["PASW"], sizeof(PASW));
    PORT = JSON_DOC["PORT"].as<int>(); 
    HASS = JSON_DOC["HASS"].as<boolean>();Serial.println(HASS);
    ENAB = JSON_DOC["ENAB"].as<boolean>();Serial.println(ENAB);
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(164); 
    JSON_DOC["HOST"] = HOST;
    JSON_DOC["USER"] = USER; 
    JSON_DOC["PASW"] = PASW; 
    JSON_DOC["PORT"] = PORT; 
    JSON_DOC["HASS"] = HASS;
    JSON_DOC["ENAB"] = ENAB;
    JSON_DOC["TOPC"] = TOPC;
    char json_string[164];
    serializeJson(JSON_DOC, json_string); 
    File dataFile = LittleFS.open("Mqtt.json", "w");
    if (!dataFile) {Serial.println("Failed to open config file for writing"); return false;}
    if(dataFile.print(json_string)){Serial.println("- file written");} else {Serial.println("- write failed");}
    dataFile.close();
    return true;
  }
};

// --------------------------------------------------------------------------------------
// Timer für Brenner Lauf- und Wartezeit
// --------------------------------------------------------------------------------------
ulong TimerBurnMs = 0;              // Messung länge Brennvorgang in ms
ulong TimerWaitMs = millis();       // Messung Zeitabstand zwischen Brennvorgängen in Ms

// --------------------------------------------------------------------------------------
// Definitionen für letzte Zustände und Berechnungen
// --------------------------------------------------------------------------------------
String LastSensRGB = "255,255,255";   // 255,... ist weiß und Fehler beim Sensor.
int    LastBurnStat  = 0;             // Letzter Brennerstatus

// --------------------------------------------------------------------------------------
// Hilfsvariablen
// --------------------------------------------------------------------------------------
ulong Timer2s = millis();          // Strategie für Werte senden (ForcedSend).
ulong Timer25s = millis();         // Strategie für Werte senden (ForcedSend).
ulong Timer60s = millis();         // Strategie für Werte senden (ForcedSend).
bool APMode = true;                // Wenn AP-Mode dann MQTT ignorieren
bool TCS = true;                   // Wenn TCS34725 verwendet wurde, ansonsten APDS9960
bool SensReady = false;            // Wenn Sensor init OK
bool MqttReady = false;            // Wenn MQTT init OK
int numberOfNetworks = 0;          // Anzahl gefundener Wlan-Netzwerke
const byte pinLED = D6;            // LED-Pin bei TCS-Sensor/Int bei APDS
const byte ECHO = D8;              // HC-SR04 Echo
const byte TRIG = D7;              // HC-SR04 Trig
long duration;                     // Variable um die Zeit der Ultraschall-Wellen zu speichern
float distance;                    // Variable um die Entfernung zu berechnen
bool LightOn = true;               // Licht ein oder ausgeschaltet

// --------------------------------------------------------------------------------------
// Strukturen für die Verwaltung
// --------------------------------------------------------------------------------------
MQTTStruct  MQTTSETT {};
SENSStruct  SENSSETT {}; 
WIFIStruct  WIFISETT {}; 
BURNStruct  BURNSETT {};
USAGEStruct USAGE    {};

Adafruit_TCS34725 tcs = Adafruit_TCS34725();
SparkFun_APDS9960 apds = SparkFun_APDS9960 ();
espMqttClientAsync mqttClient;
AsyncEventSource events("/events"); 
AsyncWebServer server(80);

void setup(void) {
  Serial.begin(115200);                   // Serielle Konsole aktivieren (115200)
  Serial.println("");Serial.println("");  // Leerzeilen um vom Bootcode abzusetzen
  pinMode (pinLED, OUTPUT);               // LED-Pin des Sensors.
  pinMode (TRIG, OUTPUT);                 // TRIG-Pin: Output
  pinMode (ECHO, INPUT);                  // ECHO-Pin: Input

  // Disable the Watchdog (Debugging)
  // ESP.wdtDisable();
  // *((volatile uint32_t*) 0x60000900) &= ~(1); // Hardware WDT OFF
  // 0 -> normal startup by power on
  // 1 -> hardware watch dog reset
  // 2 -> software watch dog reset (From an exception)
  // 3 -> software watch dog reset system_restart (Possibly unfed watchdog got angry)
  // 4 -> soft restart (Possibly with a restart command)
  // 5 -> wake up from deep-sleep

  InitLittleFS ();                        // Im LittleFS stehen die WLAN- und andere daten.
  numberOfNetworks = WiFi.scanNetworks(); // Bevor zu einem WLAN verbunden wird, sonst Absturz!

  bool LoadedWifi = WIFISETT.Load ();
  if(WifiRestore == true) {WIFISETT.SSID = WIFISSID; WIFISETT.PASS = WIFIPASS;};

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

  // Wenn vorhanden dann laden ansonsten von Defaults nehmen und abspeichern.
  MQTTSETT.Load();
  SENSSETT.Load();
  BURNSETT.Load();
  USAGE.Load();

  InitMQTT();                                                          // MQTT-Initialisieren

  Serial.println (SENSSETT.TYPE);
  if (strcmp(SENSSETT.TYPE, "TCS34725") == 0) {InitTCS34725();}        // entweder TCS-Sensor Initialisieren (Standard)
  if (strcmp(SENSSETT.TYPE, "APDS9960") == 0) {InitGYP9960 ();}        // oder ... GYP-Sensor Initialisieren
 
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

  // HTML-Inhalte (LittleFS)
  server.on("/Burn.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, "Burn.json", "text/html");});
  server.on("/Wifi.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, "Wifi.json", "text/html");});
  server.on("/Mqtt.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, "Mqtt.json", "text/html");});
  server.on("/Sens.json",   HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, "Sens.json", "text/html");});
  server.on("/Usage.json",  HTTP_GET,   [](AsyncWebServerRequest *request){ request->send(LittleFS, "Usage.json","text/html");});

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
    bool UsageUpdated = false;

    for(int i=0;i<paramsNr;i++){
        AsyncWebParameter* p = request->getParam(i);

        if (p -> name() == "SSID") {WIFISETT.SSID  = p-> value().c_str(); WifiUpdated = true;}
        if (p -> name() == "PASS") {WIFISETT.PASS  = p-> value().c_str(); WifiUpdated = true;}
		
        if (p -> name() == "INTEG"){SENSSETT.INTEG = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "GAIN") {SENSSETT.GAIN  = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "LIGHT"){char BUF[8];     p-> value().toCharArray (BUF, 8);  strcpy (SENSSETT.LIGHT, BUF);}
        if (p -> name() == "TYPE") {char BUF[16];    p-> value().toCharArray (BUF, 16); strcpy (SENSSETT.TYPE , BUF);}
        
        //if (p -> name() == "HOST") {MQTTSETT.HOST  =  p-> value().c_str();  MqttUpdated = true;}
        //if (p -> name() == "USER") {MQTTSETT.USER  =  p-> value().c_str();  MqttUpdated = true;}
        //if (p -> name() == "PASW") {MQTTSETT.PASW  =  p-> value().c_str();  MqttUpdated = true;}
        //if (p -> name() == "TOPC") {MQTTSETT.TOPC  =  p-> value().c_str();  MqttUpdated = true;}
        if (p -> name() == "PORT") {MQTTSETT.PORT  =  p-> value().toInt();  MqttUpdated = true;}
        if (p -> name() == "ENAB") {MQTTSETT.ENAB  = cBool(p-> value().c_str()); MqttUpdated = true;}
        if (p -> name() == "HASS") {MQTTSETT.HASS  = cBool(p-> value().c_str()); MqttUpdated = true;}
        
        if (p -> name() == "TOL")     {BURNSETT.TOL        = p-> value().toInt();    BurnUpdated = true;}
        if (p -> name() == "MAX")     {BURNSETT.MAX        = p-> value().toInt();    BurnUpdated = true;}
        if (p -> name() == "ACT")     {BURNSETT.ACT        = p-> value().toDouble(); BurnUpdated = true;}
        if (p -> name() == "ACT")     {USAGE.ActTankL      = p-> value().toDouble(); UsageUpdated = true;}
        if (p -> name() == "COR")     {BURNSETT.COR        = p-> value().toDouble(); BurnUpdated = true;}
        if (p -> name() == "L_H")     {BURNSETT.L_H        = p-> value().toDouble(); BurnUpdated = true;}
        if (p -> name() == "LKW")     {BURNSETT.LKW        = p-> value().toDouble(); BurnUpdated = true;}
        if (p -> name() == "PREH_R")  {BURNSETT.PREHEAT_R  = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "PREH_G")  {BURNSETT.PREHEAT_G  = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "PREH_B")  {BURNSETT.PREHEAT_B  = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BLOW_R")  {BURNSETT.BLOW_R     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BLOW_G")  {BURNSETT.BLOW_G     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BLOW_B")  {BURNSETT.BLOW_B     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BURN_R")  {BURNSETT.BURN_R     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BURN_G")  {BURNSETT.BURN_G     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "BURN_B")  {BURNSETT.BURN_B     = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "ERRO_R")  {BURNSETT.ERROR_R    = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "ERRO_G")  {BURNSETT.ERROR_G    = p-> value().toInt(); BurnUpdated = true;}
        if (p -> name() == "ERRO_B")  {BURNSETT.ERROR_B    = p-> value().toInt(); BurnUpdated = true;}
    }

    if (MqttUpdated == true)  {MQTTSETT.Save(); Serial.print("Saved Mqtt-Settings.");};
	  if (SensUpdated == true)  {SENSSETT.Save(); Serial.print("Saved Sensor-Settings.");};
    if (WifiUpdated == true)  {WIFISETT.Save(); Serial.print("Saved Wifi-Settings.");};
    if (BurnUpdated == true)  {BURNSETT.Save(); Serial.print("Saved Burn-Settings.");};
    if (UsageUpdated == true) {USAGE.Save();    Serial.print("Saved Usage Settings.");};
    
    request->send(200, "text/plain", "Parameters received");
  });

  // Kommandos und Befehle
  server.on("/restart",   HTTP_GET,   [](AsyncWebServerRequest *request){ ESP.restart(); });

  server.on("/LightSwitch",     HTTP_GET,   [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "switched light state.");
    if (LightOn == true){
      digitalWrite(pinLED, LOW);
      LightOn = false;
    } else {
      digitalWrite(pinLED, HIGH);
      LightOn = true;
    };
  });

  // -----------------------------------------------------------------------------
  // Diese Funktion dient dazu die Werte direkt aus den Structs zu lesen weil 
  // beim einlesen der Dateien -> Konvertieren zu JSON -> Werte ausgeben bei
  // zu wenig Json-Speicher einfach 0 ausgegeben wird. Ist der letzte Wert einer
  // Struct nicht der angegebene Wert der JSON, so sollte der Speicher des Doks 
  // erhöht werden. 
  // -----------------------------------------------------------------------------
  server.on("/ReadMem",     HTTP_GET,   [](AsyncWebServerRequest *request){
    AsyncResponseStream  *response = request->beginResponseStream("text/plain");
    response->println(BURNSETT.ERROR_R);
    response->println(BURNSETT.ERROR_G);
    response->println(BURNSETT.ERROR_B);
    response->println(USAGE.ActTankL);
    request->send(response);
  });

  server.on("/RecoverUsage",     HTTP_GET,   [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Sensor and Burn setting resetted. Reboot.");
    LittleFS.remove("Usage.json"); 
    ESP.restart();
  });
  server.on("/RecoverNETW",     HTTP_GET,   [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Sensor and Burn setting resetted. Reboot.");
    LittleFS.remove("Wifi.json"); 
    LittleFS.remove("Mqtt.json");
    ESP.restart();
  });
  server.on("/RecoverBSET",     HTTP_GET,   [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Sensor and Burn setting resetted.");
    LittleFS.remove("Burn.json"); 
    LittleFS.remove("Sens.json"); 
    ESP.restart();
  });

  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.addHandler(&events);
  server.begin();

  Serial.println ("Setup finished. starting main loop.");
}

void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
  if (APMode == false && MQTTSETT.ENAB == true){
    Serial.printf("Disconnected from MQTT: %u.\n", static_cast<uint8_t>(reason));
    MqttReady = false; // delay (1000); // Delay here will cause a restart in Async!
    mqttClient.connect();
  }
}
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  MqttReady = true;
  PublishHASS();
}

void loop(void) {

  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);  duration = pulseIn(ECHO, HIGH); distance = duration * 0.034 / 2;
  Serial.println("Entfernung: " + String(distance) + "cm");
  Serial.println ("Liter Verbleibend:" + String(LiterfromDistance(distance)) + " L");
  
// -------------------------------------------------------------
// Forcing von Werten alle X Sekunden (unabhängig von Sensoren)
// -------------------------------------------------------------
bool Force2S = false;  if (millis() - Timer2s >= 2000)   {Force2S = true; Timer2s    = millis ();} // ESP-Heap, Color + Status
bool Force25S = false; if (millis() - Timer25s >= 25000) {Force25S = true; Timer25s  = millis ();} // Consumption values
bool Force60S = false; if (millis() - Timer60s >= 60000) {Force60S  = true; Timer60s = millis ();} // Hass-Republish

  if (Force2S){   // Heap + Reset
    events.send(ESP.getResetReason().c_str(), "LastReset"); 
    events.send(String(ESP.getFreeHeap()).c_str (), "ESPHeap"); 
  }
  if (Force25S){  // Sensor ready
    if (SensReady == true){
      uint8_t x = tcs.read8(TCS34725_ID);
      if ((x != 0x4d) && (x != 0x44) && (x != 0x10)) {
        SensReady = false;
        events.send("Sensor antwortet nicht.", "LastSensRGB");
        Serial.println("Sensor antwortet nicht.");
      }
    }
  }
  if (Force60S){  // Hass-Publish
    PublishHASS();
  }

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
  int S = BrennerStatus(R,G,B);

  // ----------------------------------------------------------
  // Bei Wertänderung oder alle 2 Sekunden Farbstatus senden
  // ----------------------------------------------------------
  if (RGB != LastSensRGB || Force2S){
    events.send(RGB.c_str(), "LastSensRGB");  
    events.send(String(S).c_str(), "LastBurnStat");  
    if (MqttReady == true){
      mqttClient.publish("oilmeter/sensor/0/R", 0, true, String(R).c_str());
      mqttClient.publish("oilmeter/sensor/0/G", 0, true, String(G).c_str());
      mqttClient.publish("oilmeter/sensor/0/B", 0, true, String(B).c_str());
      mqttClient.publish("oilmeter/sensor/0/L", 0, true, String(L).c_str());
    }
  }

  if (S == 3 && LastBurnStat != 3){                                                                      // Wenn brennen startet
    TimerBurnMs = millis();                                                                              // Start des Brenntimers
    USAGE.LastWaitM = (((double)millis() - (double)TimerWaitMs)) / 60000.0;                              // letzte Wartezeit setzen
    USAGE.GesWaitM += USAGE.LastWaitM;                                                                   // gesamte Wartezeit setzen
    USAGE.Save();
  }  

  if (S == 3){                                                                                           // Während dem Brennen  
    double TimeS = (double)(millis() - TimerBurnMs) / 1000;                                             
    double Verbr = TimeS  * (BURNSETT.L_H / 3600.0) * BURNSETT.COR;                                      // Berechnen des Verbrauchs in Liter
    double GenkW = Verbr * BURNSETT.LKW;                                                                 // Berechnen der generierung in kW
    events.send(String(TimeS,2).c_str(), "LastBurnM"); 
    events.send(String(Verbr,4).c_str(), "LastBurnL"); 
    events.send(String(GenkW,4).c_str(), "LastGenkW"); 
  }  
  if (S != 3){                                                                                           // Während dem Warten 
    double TimeS = (double)(millis() - TimerWaitMs) / 1000;                                              
    events.send(String(TimeS).c_str(), "LastWaitM"); 
  }  

  if (S != 3 && LastBurnStat == 3){                                                                      // Wenn brennen stoppt
    TimerWaitMs = millis();                                                                              // Brennabstand rückstellen
    USAGE.LastBurnM = (double)(millis() - TimerBurnMs) / 1000.0;                                         // Brennzeit merken und auf Sek. anpassen
    USAGE.LastBurnL = USAGE.LastBurnM  * (BURNSETT.L_H / 3600.0) * BURNSETT.COR;                         // Berechnen des Verbrauchs in Liter
    USAGE.LastGenkW = USAGE.LastBurnL * BURNSETT.LKW;                                                    // Berechnen der generierung in kW

    USAGE.GesBurnL += USAGE.LastBurnL;                                                                   // Gesamtanzahl Liter ermitteln
    USAGE.GesGenkW += USAGE.LastGenkW;                                                                   // Gesamtanzahl kW ermitteln
    USAGE.GesBurnM += USAGE.LastBurnM / 60;                                                              // Gesmtanzahl Brennerlaufzeit (Minuten)
    USAGE.ActTankL -= USAGE.LastBurnL;                                                                   // Tankinhalt reduzieren

    // Serial.println ("--------------------------------------------------------------");
    // Serial.print ("Burn_s: ");    Serial.print (LastBurnS,6);     Serial.println (" Sekunden");
    // Serial.print ("Burn_l: ");    Serial.print (LastBurnL,6);     Serial.println (" Liter");
    // Serial.print ("Burn_kW: ");   Serial.print (LastGenkW,6);     Serial.println (" kWh");
    // Serial.print ("Total_S: ");   Serial.print (GesBurnS,6);      Serial.println (" Sekunden");
    // Serial.print ("Total_L: ");   Serial.print (GesBurnL,6);      Serial.println (" Liter");
    // Serial.print ("Total_kW: ");  Serial.print (GesGenkW,6);      Serial.println (" kWh");
    // Serial.println ("--------------------------------------------------------------");
    USAGE.Save();
  }


  // --------------------------------------------------------------------------------------
  // Wenn sich der Brennerstatus ändert oder Homeassistant ein Update braucht (25s)
  // --------------------------------------------------------------------------------------
  if (S != LastBurnStat || Force25S){
    events.send(String(S).c_str(), "LastBurnStat");
    events.send(String(USAGE.GesBurnM,6).c_str(), "GesBurnM"); 
    events.send(String(USAGE.GesWaitM,6).c_str(), "GesWaitM"); 
    events.send(String(USAGE.GesBurnL,6).c_str(), "GesBurnL"); 
    events.send(String(USAGE.GesGenkW,6).c_str(), "GesGenkW");
    events.send(String(USAGE.LastBurnM,6).c_str(), "LastBurnM"); 
    events.send(String(USAGE.LastWaitM,6).c_str(), "LastWaitM"); 
    events.send(String(USAGE.LastBurnL,6).c_str(), "LastBurnL"); 
    events.send(String(USAGE.LastGenkW,6).c_str(), "LastGenkW"); 

    if (MqttReady == true){
      mqttClient.publish("oilmeter/LastBurnStat", 0, true, String(S).c_str());
      mqttClient.publish("oilmeter/LastBurnM", 0, true, String(USAGE.LastBurnM,6).c_str());
      mqttClient.publish("oilmeter/LastWaitM", 0, true, String(USAGE.LastWaitM,6).c_str());
      mqttClient.publish("oilmeter/LastBurnL", 0, true, String(USAGE.LastBurnL,6).c_str());
      mqttClient.publish("oilmeter/LastGenkW", 0, true, String(USAGE.LastGenkW,6).c_str());
      mqttClient.publish("oilmeter/GesBurnM", 0, true, String(USAGE.GesBurnM,6).c_str());
      mqttClient.publish("oilmeter/GesWaitM", 0, true, String(USAGE.GesWaitM,6).c_str());
      mqttClient.publish("oilmeter/GesBurnL", 0, true, String(USAGE.GesBurnL,6).c_str());
      mqttClient.publish("oilmeter/GesGenkW", 0, true, String(USAGE.GesGenkW,6).c_str());
      mqttClient.publish("oilmeter/ActTankL", 0, true, String(USAGE.ActTankL,6).c_str());
      mqttClient.publish("oilmeter/MaxTankL", 0, true, String(BURNSETT.MAX).c_str());
    }
  }

  LastSensRGB = RGB;      // Set last Sensor Status
  LastBurnStat = S;       // Set last Status
  delay (250); 
  yield();

} else {
  if (Force2S){
    // MQTT alles 255 - um in Homeassistant bekannt zu machen dass hier ein Fehler ist
    mqttClient.publish("oilmeter/sensor/0/R", 0, true, String(128).c_str());
    mqttClient.publish("oilmeter/sensor/0/G", 0, true, String(128).c_str());
    mqttClient.publish("oilmeter/sensor/0/B", 0, true, String(128).c_str());
    events.send("128,128,128", "LastSensRGB");
    events.send("Sensor Fehler!", "LastSensRGB");
    Serial.println ("Sensor Fehler!");
    delay (2000);
  }
}
delay (1000);
}

double LiterfromDistance (double Measurement){
  // Testvariablen
  double Hoehe    =  100; // cm
  double LenOben  =  20;  // cm
  double LenUnten =  10;  // cm
  double Abstand  =  10;  // cm
  double LiterGes =  250; // L
  double HoeheInv   = Abstand + Hoehe -  Measurement;
  double LiterCalc  = (Hoehe * 3.1415 / 12 * ((LenOben*LenOben) + LenOben * LenUnten + (LenUnten * LenUnten))) / 1000;
  double FuellBreit = LenUnten + (LenOben - LenUnten) * HoeheInv / Hoehe;
  double LiterAct   = (HoeheInv * 3.1415 / 12 * ((FuellBreit*FuellBreit) + FuellBreit * LenUnten + (LenUnten * LenUnten))) / 1000;

  // Serial.print ("VolCalc:"); Serial.println (LiterCalc);
  // Serial.print ("VolAct:"); Serial.println (LiterAct);
  // Serial.print ("FuellBreit:"); Serial.println (FuellBreit);
  // Serial.print ("Percent:"); Serial.println (LiterAct / LiterCalc);
  // Serial.print ("Liter:"); Serial.println (LiterGes * LiterAct / LiterCalc);

  return LiterGes * LiterAct / LiterCalc;
}

int BrennerStatus (int &R,int &G,int &B){
  // Aus (0) als undefinierter Zustand (auch zwischen den Bereichen)
  int Status = 0; 
  // Vorheizen
  if (R >= BURNSETT.PREHEAT_R - BURNSETT.TOL && 
      R < BURNSETT.PREHEAT_R + BURNSETT.TOL &&
      G >= BURNSETT.PREHEAT_G - BURNSETT.TOL && 
      G < BURNSETT.PREHEAT_G + BURNSETT.TOL &&
      B >= BURNSETT.PREHEAT_B - BURNSETT.TOL && 
      B < BURNSETT.PREHEAT_B + BURNSETT.TOL ){
      Status = 1; // Vorheizen
  }
  // Gebläsestart
  if (R >= BURNSETT.BLOW_R - BURNSETT.TOL && 
      R < BURNSETT.BLOW_R + BURNSETT.TOL &&
      G >= BURNSETT.BLOW_G - BURNSETT.TOL && 
      G < BURNSETT.BLOW_G + BURNSETT.TOL &&
      B >= BURNSETT.BLOW_B - BURNSETT.TOL && 
      B < BURNSETT.BLOW_B + BURNSETT.TOL ){
      Status = 2; // Gebläsestart
  }
  // Brennen
  if (R >= BURNSETT.BURN_R - BURNSETT.TOL && 
      R < BURNSETT.BURN_R + BURNSETT.TOL &&
      G >= BURNSETT.BURN_G - BURNSETT.TOL && 
      G < BURNSETT.BURN_G + BURNSETT.TOL &&
      B >= BURNSETT.BURN_B - BURNSETT.TOL && 
      B < BURNSETT.BURN_B + BURNSETT.TOL ){
      Status = 3; // Brennen
  }
  // Brennen
  if (R >= BURNSETT.ERROR_R - BURNSETT.TOL && 
      R < BURNSETT.ERROR_R + BURNSETT.TOL &&
      G >= BURNSETT.ERROR_G - BURNSETT.TOL && 
      G < BURNSETT.ERROR_G + BURNSETT.TOL &&
      B >= BURNSETT.ERROR_B - BURNSETT.TOL && 
      B < BURNSETT.ERROR_B + BURNSETT.TOL ){
      Status = 4; // Störung
  }
  return Status;
}

bool InitWifiSTA (){

if (WifiRestore == true){
  Serial.println("Reset Wifi credentials");
  WIFISETT.SSID = WIFISSID;
  WIFISETT.PASS = WIFIPASS;
}

  WiFi.mode(WIFI_STA);                      // Modus Station
  WiFi.begin(WIFISETT.SSID, WIFISETT.PASS);   // SSID, Kennwort
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
  Serial.println(WIFISETT.SSID);
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

  Serial.print ("Connect using: Host:");
  Serial.print (MQTTSETT.HOST);
  Serial.print (", User:");
  Serial.print (MQTTSETT.USER);
  Serial.print (", Password: ");
  Serial.println (MQTTSETT.PASW);

  if (APMode == true)         {Serial.println("No MQTT while in AP-Mode.");return; } 
  if (MQTTSETT.ENAB == false) {Serial.println("Cancel MQTT init (disabled).");return; }

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  IPAddress ADDR; ADDR.fromString(MQTTSETT.HOST); 
  mqttClient.setCredentials(MQTTSETT.USER, MQTTSETT.PASW); // .setClientId(String(ESP.getChipId()).c_str());
  mqttClient.setServer(ADDR, MQTTSETT.PORT); 
  mqttClient.connect();
}
void InitGYP9960 (){
  TCS = false;
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
  TCS = true;
  if (SENSSETT.GAIN == 1)  {tcs.setGain (TCS34725_GAIN_1X);}
  if (SENSSETT.GAIN == 4)  {tcs.setGain (TCS34725_GAIN_4X);}
  if (SENSSETT.GAIN == 16) {tcs.setGain (TCS34725_GAIN_16X);}
  if (SENSSETT.GAIN == 60) {tcs.setGain (TCS34725_GAIN_60X);}
  if (SENSSETT.INTEG == 2.4) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_2_4MS);}
  if (SENSSETT.INTEG == 24) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_24MS);}
  if (SENSSETT.INTEG == 50) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_50MS);}
  if (SENSSETT.INTEG == 101) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_101MS);}
  if (SENSSETT.INTEG == 154) {tcs.setIntegrationTime (TCS34725_INTEGRATIONTIME_154MS);}
  if (SENSSETT.INTEG == 700) {tcs.setIntegrationTime ( TCS34725_INTEGRATIONTIME_614MS);}
  if (tcs.begin()) {
    Serial.println("Found TCS34725 sensor");

    if (strcmp(SENSSETT.LIGHT, "False") == 0) {
      Serial.println("set LED off");
      digitalWrite(pinLED, LOW);
      LightOn = false;
    } else {
      Serial.println("set LED on");
      digitalWrite(pinLED, HIGH);
      LightOn = true;
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

const char origArray[][7][60]= {
  // HASS-NODE                                             NAME                   STATE-TOPIC               UNIQUE-ID                 MEAS-Unit      DEV-CLA  STATE-CLASS
    {"homeassistant/sensor/oilmeter/color_r/config",      "Color_R",              "oilmeter/sensor/0/R",   "OilMeter_Sensor0_R",      "RGB",         "gas", "measurement"},
    {"homeassistant/sensor/oilmeter/color_g/config",      "Color_G",              "oilmeter/sensor/0/G",   "OilMeter_Sensor0_G",      "RGB",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/color_b/config",      "Color_B",              "oilmeter/sensor/0/B",   "OilMeter_Sensor0_B",      "RGB",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/color_l/config",      "Lux",                  "oilmeter/sensor/0/L",   "OilMeter_Sensor0_L",      "RGB",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastBurnM/config",    "Letze Brenndauer",     "oilmeter/LastBurnM",    "OilMeter_LastBurnM",      "Sec",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastWaitM/config",    "Letze Wartedauer",     "oilmeter/LastWaitM",    "OilMeter_LastWaitM",      "Sec",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastBurnL/config",    "Letze Brennmenge",     "oilmeter/LastBurnL",    "OilMeter_LastBurnL",      "L",           "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/LastGenkW/config",    "Letze Generierung",    "oilmeter/LastGenkW",    "OilMeter_LastGenkW",      "kWh",         "gas", "measurement"}, 
    {"homeassistant/sensor/oilmeter/GesBurnM/config",     "Gesamte Brenndauer",   "oilmeter/GesBurnM",     "OilMeter_GesBurnM",       "Min",         "gas", "total_increasing"}, 
    {"homeassistant/sensor/oilmeter/GesWaitM/config",     "Gesamte Wartedauer",   "oilmeter/GesWaitM",     "OilMeter_GesWaitM",       "Min",         "gas", "total_increasing"}, 
    {"homeassistant/sensor/oilmeter/GesBurnL/config",     "Gesamte Brennmenge",   "oilmeter/GesBurnL",     "OilMeter_GesBurnL",       "L",           "gas", "total_increasing"}, 
    {"homeassistant/sensor/oilmeter/GesGenkW/config",     "Gesamte Generierung",  "oilmeter/GesGenkW",     "OilMeter_GesGenkW",       "kWh",         "gas", "total_increasing"}, 
    {"homeassistant/sensor/oilmeter/ActTankL/config",     "Tankinhalt_Aktuell",   "oilmeter/ActTankL",     "OilMeter_ActTankL",       "L",           "gas", "total_increasing"}, 
    {"homeassistant/sensor/oilmeter/MaxTankL/config",     "Tankinhalt_Maximal",   "oilmeter/MaxTankL",     "OilMeter_MaxTankL",       "kWh",         "gas", "measurement"},
    {"homeassistant/sensor/oilmeter/LastBurnStat/config", "Letzer Brennerstatus", "oilmeter/LastBurnStat", "OilMeter_LastBurnStat",    "",           "gas", "measurement"} 
};
void PublishHASS (){ 
  if (MQTTSETT.HASS == false){Serial.println ("HASS disabled");     return; }
  if (MqttReady     == false){Serial.println ("MQTT not ready");    return; };

  // mqttClient.publish("homeassistant/sensor/oilmeter/color_l/config",0,true, "{\"name\": \"Consumption\",\"stat_t\":\"oilmeter/Consumption_l\",  \"uniq_id\": \"oilmeter_Consumption_l\",  \"unit_of_meas\": \"L\",  \"dev\": {    \"name\": \"oilmeter\",   \"ids\": \"oilmeter_1\",    \"cu\": \"http://192.168.7.107\",\"mf\": \"oilmeter\",    \"mdl\": \"tcs34725\",    \"sw\": \"v001\"  },  \"exp_aft\": 15,  \"dev_cla\": \"water\",  \"stat_cla\": \"measurement\"}");
  // https://www.home-assistant.io/integrations/mqtt/

      Serial.println ("(re-)publishing HASS-Topics");
      int size = sizeof (origArray) / sizeof (origArray[0]);
      String URL = "http://" + WiFi.localIP().toString();

      for (int i = 0; i < size; i++){
        // lt. Dokumentation besser das Dokument neu zu erstellen anstatt zu ändern
        // weil die alten Werte im Heap bestehen bleiben und Speicher fressen.
        DynamicJsonDocument HASSETT(512);

        HASSETT["dev"]["name"]   =  MQTTSETT.TOPC;
        HASSETT["dev"]["ids"]    = "oilmeter_1";
        HASSETT["dev"]["cu"]     =  URL; // IP
        HASSETT["dev"]["mf"]     = "Phreak87";
        HASSETT["dev"]["mdl"]    = "ESP8266+TCS34725/APDS9960";
        HASSETT["dev"]["sw"]     = "V 1.0";
        HASSETT["name"]          = origArray[i][1];
        HASSETT["stat_t"]        = origArray[i][2];
        HASSETT["uniq_id"]       = origArray[i][3];
        HASSETT["unit_of_meas"]  = origArray[i][4];
        HASSETT["exp_aft"]       = 30;
        HASSETT["dev_cla"]       = origArray[i][5];
        HASSETT["stat_cla"]      = origArray[i][6];

        char json_string[512];
        serializeJson (HASSETT,json_string);
        mqttClient.publish (origArray[i][0],0,true,json_string);
      }

      // mqttClient.loop ();
      ESP.wdtFeed();

}