#include <SPI.h>
#include <Wire.h>
#include <time.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJSON.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <espMqttClientAsync.h>
#include <Adafruit_TCS34725.h>
#include <SparkFun_APDS9960.h>
#include "Adafruit_VL53L0X.h"
#include "Structs.h"            // Strukturen
#include "Index.html"           // Startpage
#include "About.html"           // Über-Seite
#include "Wifi.html"            // WLAN-Einstellungen
#include "MQTT.html"            // MQTT-Einstellungen
#include "Default.html"         // Übersichtsseite
#include "Sensor.html"          // Sensoreinstellungen
#include "Brenner.html"         // Brennereinstellungen
#include "Style.css"            // Style für alle Seiten
#include "Menu.css"             // Style für Menü

// --------------------------------------------------------------------------------------
// WLAN Einstellungen beim Reboot überschreiben (nützlich für Wifi-Tests).
// WifiRestore auf true setzen um Einstellungen anzuwenden.
// --------------------------------------------------------------------------------------
const bool WifiRestore = false;
const char* WIFISSID = "WLAN";
const char* WIFIPASS = "PASSWORD";

bool cBool    (const char* Input){
  if (strcmp(Input, "True") == 0 || strcmp(Input, "true") == 0 || strcmp(Input, "1") == 0 ) {return true;}
  return false;
}

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
bool   LastState = false;             // Letzter Triggerstatus

// --------------------------------------------------------------------------------------
// Hilfsvariablen
// --------------------------------------------------------------------------------------
ulong Timer2s = millis();          // Strategie für Werte senden (ForcedSend).
ulong Timer20s = millis();         // Strategie für Werte senden (ForcedSend).
ulong Timer60s = millis();         // Strategie für Werte senden (ForcedSend).
ulong TimeChange = 0;              // Wenn die Uhrzeit 00:00 ist, dann muss die aktuelle zeit abgezogen werden - sonst ergeben sich mehr als 24h/Tag.
bool Force2S = false;              // Triggermerkmal wenn > 2S
bool Force20S = false;             // Triggermerkmal wenn > 2S
bool Force60S = false;             // Triggermerkmal wenn > 2S
bool APMode = true;                // Wenn AP-Mode dann MQTT ignorieren
bool TCS = true;                   // Wenn TCS34725 verwendet wurde, ansonsten APDS9960
bool SensReady = false;            // Wenn Sensor init OK
bool MqttReady = false;            // Wenn MQTT init OK
int numberOfNetworks = 0;          // Anzahl gefundener Wlan-Netzwerke
const byte trigBTN = D1;           // D1 ist Trigger
const byte pinLED = D6;            // LED-Pin bei TCS-Sensor/Int bei APDS
const byte ECHO = D8;              // HC-SR04 Echo
const byte TRIG = D7;              // HC-SR04 Trig
char NTPStart[32]   = "";          // Log Time since ESP8266 is running
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

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200, 60000);
Adafruit_TCS34725 tcs = Adafruit_TCS34725();
SparkFun_APDS9960 apds = SparkFun_APDS9960 ();
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
espMqttClientAsync mqttClient;
AsyncEventSource events("/events"); 
AsyncWebServer server(80);


void PublishHASS2 (const char (*Array)[7][32]){
  if (Force60S      == false){                                                 return;};
  if (LastBurnStat  == 3)    {Serial.println ("No publish while Burning");     return;};
  if (MQTTSETT.HASS == false){Serial.println ("HASS disabled");                return;};
  if (MqttReady     == false){Serial.println ("MQTT not ready");               return;};

      Serial.println ("(re-)publishing HASS-Topics");
      int size = sizeof (Array) -1; 
      String URL = "http://" + WiFi.localIP().toString();

      for (int i = 0; i < size; i++){
        DynamicJsonDocument HASSETT(512);
        HASSETT["dev"]["name"]   =  MQTTSETT.TOPC;
        HASSETT["dev"]["ids"]    =  MQTTSETT.TOPC;
        HASSETT["dev"]["cu"]     =  URL; // IP
        HASSETT["dev"]["mf"]     = "Phreak87";
        HASSETT["dev"]["mdl"]    = "ESP8266 + TCS34725/APDS9960";
        HASSETT["dev"]["sw"]     = "V 1.0";

        HASSETT["name"]          = Array[i][1]; // Der ANZEIGE-Name, Beschreibung
        HASSETT["unit_of_meas"]  = Array[i][4]; // Einheit
        HASSETT["exp_aft"]       = 30;          // Wert gültig sekunden
        HASSETT["dev_cla"]       = Array[i][5]; // Geräteklasse (gas weil hass kein öl kann ...)
        HASSETT["stat_cla"]      = Array[i][6]; // Zustand (summierung, aktueller wert)
        char statt[64]; sprintf(statt,"%s/%s",   MQTTSETT.TOPC, Array[i][2]); HASSETT["stat_t"]  = statt;   // oilmeter/sensor/0/R
        char unique[64]; sprintf(unique,"%s.%s", MQTTSETT.TOPC, Array[i][3]); HASSETT["uniq_id"] = unique;  // OilMeter_Sensor0_R

        char json_string[512];
        serializeJson (HASSETT,json_string);

        char topc[64]; sprintf(topc,"%s/%s/%s/config",HassSens, &MQTTSETT.TOPC, &Array[i][1]);  
        mqttClient.publish (topc,0,true, json_string);
        ESP.wdtFeed(); 
      }
      Serial.println ("Finished publishing HASS-Topics");
}

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
  WiFi.setSleep (false);                  // Stabilere und schnellere Verbindungen. 

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

  timeClient.begin(); timeClient.forceUpdate() ; delay(100);
  char Datetime[32]; time_t  epochTime = timeClient.getEpochTime();
  struct tm * ptm; ptm = localtime (&epochTime);
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;

  sprintf(Datetime,"%d.%d.%d, %s%s\n",monthDay, currentMonth, currentYear, timeClient.getFormattedTime().c_str());
  strlcpy (NTPStart, Datetime , sizeof(NTPStart)); 

  // Wenn vorhanden dann laden ansonsten von Defaults nehmen und abspeichern.
  MQTTSETT.Load();
  SENSSETT.Load();
  BURNSETT.Load();
  USAGE.Load();

  InitMQTT();                                                          // MQTT-Initialisieren

  Serial.println (SENSSETT.TYPE);
  if (strcmp(SENSSETT.TYPE, "TCS34725") == 0) {                        // entweder TCS-Sensor Initialisieren (Standard)
    InitTCS34725();
    Force60S = true; PublishHASS2 (HASSColor);
  }        
  if (strcmp(SENSSETT.TYPE, "APDS9960") == 0) {                        // oder ... GYP-Sensor Initialisieren
    InitGYP9960 ();
    Force60S = true; PublishHASS2 (HASSColor);
  }    
  if (strcmp(SENSSETT.TYPE, "TRIGGER") == 0) {                        // oder ... GYP-Sensor Initialisieren
    SensReady = true; 
    Force60S = true; PublishHASS2 (HASSColor);
  }  
  if (strcmp(SENSSETT.TYPE, "VL53L0X") == 0) {                        // oder ... Laser-Sensor Initialisieren
    InitVL53L0X ();
    Force60S = true; PublishHASS2 (HASSDistance);
  }         

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
  server.on("/favicon.ico", HTTP_GET,   [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", "");  });

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
        if (p -> name() == "HOHE"){SENSSETT.HOHE = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "ABST") {SENSSETT.ABST  = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "OBEN"){SENSSETT.OBEN = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "UNTEN") {SENSSETT.UNTEN  = p-> value().toInt(); SensUpdated = true;}
        if (p -> name() == "LIGHT"){char BUF[8];     p-> value().toCharArray (BUF, 8);  strlcpy (SENSSETT.LIGHT, BUF,sizeof (BUF)); SensUpdated = true;}
        if (p -> name() == "TYPE") {char BUF[16];    p-> value().toCharArray (BUF, 16); strlcpy (SENSSETT.TYPE , BUF,sizeof (BUF)); SensUpdated = true;}
        if (p -> name() == "TRIGM"){char BUF[8];     p-> value().toCharArray (BUF, 8);  strlcpy (SENSSETT.TRIGM, BUF,sizeof (BUF)); SensUpdated = true;}
        
        if (p -> name() == "HOST") {char BUF[16];    p-> value().toCharArray (BUF, 16); strlcpy (MQTTSETT.HOST , BUF,sizeof (BUF)); MqttUpdated = true;}
        if (p -> name() == "PASW") {char BUF[16];    p-> value().toCharArray (BUF, 16); strlcpy (MQTTSETT.PASW , BUF,sizeof (BUF)); MqttUpdated = true;}
        if (p -> name() == "USER") {char BUF[16];    p-> value().toCharArray (BUF, 16); strlcpy (MQTTSETT.USER , BUF,sizeof (BUF)); MqttUpdated = true;}
        if (p -> name() == "TOPC") {char BUF[16];    p-> value().toCharArray (BUF, 16); strlcpy (MQTTSETT.TOPC , BUF,sizeof (BUF)); MqttUpdated = true;}
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
  server.on("/restart",         HTTP_GET,   [](AsyncWebServerRequest *request){ ESP.restart(); });
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
    response->println(SENSSETT.HOHE);
    response->println(SENSSETT.UNTEN);
    response->println(SENSSETT.OBEN);
    response->println(SENSSETT.ABST);
    response->println(NTPStart);
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
    Serial.printf("Disconnected from MQTT: %u.\n", static_cast<uint8_t>(reason));
    MqttReady = false; // Try again if next 60s Timer is set in loop
}
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  MqttReady = true;
}

void loop(void) {

  Serial.print (".");   // Running indicator
  ResetDaily();         // Reset usage values if we have a new day

// -------------------------------------------------------------
// Forcing von Werten alle X Sekunden (unabhängig von Sensoren)
// -------------------------------------------------------------
Force2S  = false; if (millis() - Timer2s  >= 2000)  {Force2S  = true; Timer2s  = millis ();} // ESP-Heap, Color + Status
Force20S = false; if (millis() - Timer20s >= 25000) {Force20S = true; Timer20s = millis ();} // Consumption values
Force60S = false; if (millis() - Timer60s >= 60000) {Force60S = true; Timer60s = millis ();} // Hass-Republish
 
if (APMode == false &&     MQTTSETT.ENAB == true &&     Force60S == true &&     MqttReady == false){mqttClient.connect();}

bool HandledSensor = false;
if (strcmp ("TCS34725",SENSSETT.TYPE)   == 0 || strcmp ("APDS9960",SENSSETT.TYPE) == 0){HandleOpticalSensors ();HandledSensor = true; }
if (strcmp ("SR04_SR04T",SENSSETT.TYPE) == 0 || strcmp ("VL53L0X", SENSSETT.TYPE) == 0){HandleDistanceSensors();HandledSensor = true; }
if (strcmp ("TRIGGER",SENSSETT.TYPE) == 0 )                                            {HandleTriggerSensor();  HandledSensor = true; }
if (HandledSensor == false){yield();delay(500);}

PublishESPStats();

}

double LiterfromDistance (double Measurement){
  // Testvariablen
  double Hoehe    =  SENSSETT.HOHE;        // cm
  double LenOben  =  sqrt(SENSSETT.OBEN);  // cm
  double LenUnten =  sqrt(SENSSETT.UNTEN); // cm
  double Abstand  =  SENSSETT.ABST;        // cm
  double LiterGes =  BURNSETT.MAX;       // L
  char   Liste[256] = "100=100;90=95;80=90;70=80;";
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

// ----------------------------
// Initialisierung der Tools
// ----------------------------
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
  WiFi.softAP("OilMeter"); //  Name, Pass, Channel, Max_Conn
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
void InitLittleFS (){
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    Serial.println("Format LittleFS"); LittleFS.format();
  } else {
    Serial.println("LittleFS Filesystem Initialized");
  }
}

// ----------------------------
// Initialisierung der Sensoren
// zwingend SensReady setzen
// ----------------------------
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
void InitVL53L0X (){
   if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    SensReady = false;
   } else {
    SensReady = true;
  }
}
void InitTRIGGER (){
  pinMode(trigBTN, INPUT_PULLUP);
  SensReady = true;
}

// ------------------------------------------
// Nimmt Triggerwerte an und berechnet Zeit
// sowie die Mengen dahinter
// ------------------------------------------
bool HandleStates (bool ActState){

  // ------------------------------------------
  // ActState = true  = Verbrauch läuft
  // ActState = false = kein Verbrauch
  // ------------------------------------------

  int CALC = 0;                                                 // Berechnungsvariante Zeit
  if (strcmp ("An",SENSSETT.TRIGM)   == 0){CALC = 1;}           // Berechnungsvariante An (Von Aus nach An)
  if (strcmp ("Aus",SENSSETT.TRIGM)   == 0){CALC = 2;}          // Berechnungsvariante Aus (Von An nach Aus)
  // Serial.println (SENSSETT.TRIGM);
  // Serial.println (CALC);

  // ------------------------------------------
  // Nur berechnungen, keine Ausgabe
  // ------------------------------------------
    if (ActState == true && LastState == false){                // Wenn brennen startet
      TimerBurnMs = millis();                                   // Start des Verbauchstimers
      double TimeS = (double)(millis() - TimerWaitMs) / 1000;   // Wartezeit berechnen (Sekunden)
      USAGE.LastWaitM = TimeS;                                  // letzte Wartezeit setzen
      USAGE.DayWaitM += TimeS;                                  // gesamte Wartezeit pro Tag setzen
      USAGE.GesWaitM += TimeS / 60;                             // gesamte Wartezeit insgesamt setzen

      if (CALC == 1){                                            // 1 = Bei Statuswechsel Aus -> An (Abzug bei Einschalten)
        USAGE.ActTankL -= BURNSETT.L_H;
        Serial.print ("Reduzierung (Aus -> An) um: "); 
        Serial.println(BURNSETT.L_H);
      }            
    }

    if (ActState == false && LastState == true){                                                           // Wenn brennen stoppt
      TimerWaitMs = millis();                                                                              // Brennabstand rückstellen
      double TimeS = (double)(millis() - TimerBurnMs) / 1000;                                              // Berechnen der Zeit im Zustand
      double Verbr = 0;                                                                                    // Berechnen des Verbrauchs in Liter anhand Fixwert (USAGE = 1)
      if (CALC == 0){Verbr = TimeS  * (BURNSETT.L_H / 3600.0) * BURNSETT.COR;}                             // Berechnen des Verbrauchs in Liter anhand Zeit
      if (CALC == 2){Verbr = BURNSETT.L_H;}                                                                // Berechnen des Verbrauchs in Liter anhand Fixwert
      double GenkW = Verbr * BURNSETT.LKW;                                                                 // Berechnen der generierung in kW
      
      USAGE.LastBurnM = TimeS;                                                                             // Brennzeit merken und auf Sek. anpassen
      USAGE.LastBurnL = Verbr;                                                                             // Berechnen des Verbrauchs in Liter
      USAGE.LastGenkW = GenkW;                                                                             // Berechnen der generierung in kW

      USAGE.DayBurnM += TimeS;                                                                             // Brennzeit merken und auf Sek. anpassen
      USAGE.DayBurnL += Verbr;                                                                             // Berechnen des Verbrauchs in Liter
      USAGE.DayGenkW += GenkW;                                                                             // Berechnen der generierung in kW

      USAGE.GesBurnM += TimeS / 60;                                                                        // Gesmtanzahl Brennerlaufzeit (Minuten)
      USAGE.GesBurnL += Verbr;                                                                             // Gesamtanzahl Liter ermitteln
      USAGE.GesGenkW += GenkW;                                                                             // Gesamtanzahl kW ermitteln

      if (CALC == 0 || CALC == 2){
        USAGE.ActTankL -= USAGE.LastBurnL;                                                                   // Tankinhalt reduzieren
        Serial.print ("Reduzierung (An -> Aus) um: "); 
        Serial.println(BURNSETT.L_H);
      }
    }

    // --------------------------------------------------------------------
    // Bei jeder Statusänderung Verbrauchswerte speichern
    // --------------------------------------------------------------------
    if (ActState != LastState){USAGE.Save();}                                             

    // --------------------------------------------------------------------
    // Während dem Verbrauchen die aktuelle, die Tages und die Gesamtwerte.
    // updaten und an webevents publishen. (Last, Day, Ges) + Time, Liter, kW
    // Zeitmessung ausführen egal in welchem Zustand (Timer, Bei An, Bei Aus).
    // --------------------------------------------------------------------
    if (ActState == true && ActState == LastState){
        double TimeS = (double)(millis() - TimerBurnMs) / 1000;                           // Berechnen der Zeit im Zustand
        double Verbr = TimeS  * (BURNSETT.L_H / 3600.0) * BURNSETT.COR;                   // Berechnen des Verbrauchs in Liter
        if (CALC == 1 || CALC == 2) {Verbr = BURNSETT.L_H;}                               // Nur wenn es Timer ist, sonst Rücksetzen
        double GenkW = Verbr * BURNSETT.LKW;                                              // Berechnen der generierung in kW
        TimeChange = TimeS;                                                               // Wenn jetzt 0 Uhr ist diese Zeit abziehen.

        events.send(String(USAGE.ActTankL        ,6).c_str(),         "ActTankL"); 
        events.send(String(                 TimeS,2).c_str(),         "LastBurnM"); 
        events.send(String(                 Verbr,4).c_str(),         "LastBurnL"); 
        events.send(String(                 GenkW,4).c_str(),         "LastGenkW"); 
        events.send(String(USAGE.DayBurnM + TimeS,2).c_str(),         "DayBurnM"); 
        events.send(String(USAGE.DayBurnL + Verbr,4).c_str(),         "DayBurnL"); 
        events.send(String(USAGE.DayGenkW + GenkW,4).c_str(),         "DayGenkW"); 
        events.send(String(USAGE.GesBurnM + (TimeS / 60),2).c_str(),  "GesBurnM"); 
        events.send(String(USAGE.GesBurnL + Verbr,4).c_str(),         "GesBurnL"); 
        events.send(String(USAGE.GesGenkW + GenkW,4).c_str(),         "GesGenkW"); 
    }  

    // --------------------------------------------------------------------
    // Während dem Warten die aktuelle, die Tages und die Gesamtwerte
    // updaten und an webevents publishen. Zeitmessung ausführen egal in 
    // welchem Zustand (Timer, Bei An, Bei Aus) ausführen.
    // --------------------------------------------------------------------
    if (ActState == false && ActState == LastState){ 
      double TimeS = (double)(millis() - TimerWaitMs) / 1000;     
      TimeChange = TimeS;                                                               // Wenn jetzt 0 Uhr ist diese Zeit abziehen.

      events.send(String(USAGE.ActTankL        ,6).c_str(),         "ActTankL"); 
      events.send(String(TimeS).c_str(),                            "LastWaitM"); 
      events.send(String(TimeS + USAGE.DayWaitM,4).c_str(),         "DayWaitM"); 
      events.send(String((TimeS / 60) + USAGE.GesWaitM,2).c_str(),  "GesWaitM"); 
    }  

    // --------------------------------------------------------------------
    // Erzwungenes Publishing (HASS) oder Statusänderung
    // --------------------------------------------------------------------
    if (ActState != LastState || Force20S){                                                                 
      if (MqttReady == true){
        char Topic[32]; 
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastBurnStat");  mqttClient.publish(Topic, 0, true, String(ActState).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastBurnM");  mqttClient.publish(Topic, 0, true, String(USAGE.LastBurnM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastWaitM");  mqttClient.publish(Topic, 0, true, String(USAGE.LastWaitM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastBurnL");  mqttClient.publish(Topic, 0, true, String(USAGE.LastBurnL).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastGenkW");  mqttClient.publish(Topic, 0, true, String(USAGE.LastGenkW).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "DayBurnM");  mqttClient.publish(Topic, 0, true, String(USAGE.DayBurnM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "DayWaitM");  mqttClient.publish(Topic, 0, true, String(USAGE.DayWaitM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "DayBurnL");  mqttClient.publish(Topic, 0, true, String(USAGE.DayBurnL).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "DayGenkW");  mqttClient.publish(Topic, 0, true, String(USAGE.DayGenkW).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "GesBurnM");  mqttClient.publish(Topic, 0, true, String(USAGE.GesBurnM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "GesWaitM");  mqttClient.publish(Topic, 0, true, String(USAGE.GesWaitM).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "GesBurnL");  mqttClient.publish(Topic, 0, true, String(USAGE.GesBurnL).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "GesGenkW");  mqttClient.publish(Topic, 0, true, String(USAGE.GesGenkW).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "ActTankL");  mqttClient.publish(Topic, 0, true, String(USAGE.ActTankL).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "MaxTankL");  mqttClient.publish(Topic, 0, true, String(BURNSETT.MAX).c_str());
      }
    }

    LastState = ActState;       // Set last Status

    return true;
  } 

bool HandleTriggerSensor (){
  int S = digitalRead(D1); delay (50);
  int S2 = digitalRead(D1);
  if (S != S2){S = LastBurnStat;}                   // Unsicher bei Bounce, zurücksetzen auf letzten sicheren Status
  HandleStates (S == 0 ? true : false);             // Bewerten des aktuellen Status und ausgeben an Web, MQTT, Serial

  events.send(String(S).c_str(), "LastSens");       // Aktuellen Status publishen
  events.send(String(S).c_str(), "LastBurnStat");   // Aktuellen Status publishen

  PublishHASS2(HASSColor);                          // ziemlich gleich zum ColorSensor
  delay (250); yield();                             // Wartezeit bei Triggersensoren
  return true;
}
bool HandleOpticalSensors (){

  // Prüfen ob der Sensor noch funktioniert ...
  if (Force20S){  
    if (TCS){
      if (SensReady == true){
        uint8_t x = tcs.read8(TCS34725_ID);
        if ((x != 0x4d) && (x != 0x44) && (x != 0x10)) {
          SensReady = false;
          events.send("Sensor antwortet nicht mehr.", "LastSens");
          Serial.println("Sensor antwortet nicht mehr."); 
        }
      }
    }
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
    int S = BrennerStatus(R,G,B); HandleStates(S == 3 ? 1 : 0);

    // ----------------------------------------------------------
    // Bei Farbänderung oder alle 2 spezifischen Status senden
    // ----------------------------------------------------------
    if (RGB != LastSensRGB || Force2S){
      events.send(RGB.c_str(),        "LastSens");  
      events.send(String(S).c_str(),  "LastBurnStat");
      if (MqttReady == true){   
        char Topic[32]; 
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/R");  mqttClient.publish(Topic, 0, true, String(R).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/G");  mqttClient.publish(Topic, 0, true, String(G).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/B");  mqttClient.publish(Topic, 0, true, String(B).c_str());
        sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/L");  mqttClient.publish(Topic, 0, true, String(L).c_str());
      }
    }

    LastSensRGB = RGB;      // Set last Sensor Status
    LastBurnStat = S;       // Set last Status

    PublishHASS2(HASSColor);
    delay (250); yield();
    return true;

  } else {
    if (Force2S){
      // MQTT alles 255 - um in Homeassistant bekannt zu machen dass hier ein Fehler ist
      char Topic[32]; 
      sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/R");  mqttClient.publish(Topic, 0, true, String(128).c_str());
      sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/G");  mqttClient.publish(Topic, 0, true, String(128).c_str());
      sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "sensor/0/B");  mqttClient.publish(Topic, 0, true, String(128).c_str());
      events.send("Sensor Fehler!", "LastSens");
      Serial.println ("Sensor Fehler!");
    };
  }
  return true ;
}
bool HandleDistanceSensors (){

  if (strcmp(SENSSETT.TYPE, "VL53L0X") == 0) { 
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
    if (measure.RangeStatus != 4) {  // phase failures have incorrect data
      distance = measure.RangeMilliMeter / 10;
    } else {
      distance = 9999;
    }
  } 
  if (strcmp(SENSSETT.TYPE, "SR04_SR04T") == 0) {
    digitalWrite(TRIG, LOW);  delayMicroseconds(2);
    digitalWrite(TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG, LOW);  duration = pulseIn(ECHO, HIGH); 
    distance = duration * 0.034 / 2;
  } 

  // ----------------------------------------------------------
  // spezifischen Status senden
  // ----------------------------------------------------------
  events.send(String(LiterfromDistance(distance),6).c_str(), "ActTankL"); 
  events.send(String(BURNSETT.MAX).c_str(), "MaxTankL"); 
  events.send(String(distance).c_str(),     "LastSens");  
  events.send(String("none").c_str(),       "LastBurnStat");

  if (MqttReady == true){
    char Topic[32]; 
    sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "ActTankL");  mqttClient.publish(Topic, 0, true, String(LiterfromDistance(distance)).c_str());
    sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "MaxTankL");  mqttClient.publish(Topic, 0, true, String(BURNSETT.MAX).c_str());
    sprintf(Topic,"%s/%s",MQTTSETT.TOPC, "LastSens");  mqttClient.publish(Topic, 0, true, String(distance).c_str());
  }

  PublishHASS2(HASSDistance);
  delay (250); yield();
  return true ;
}

void PublishESPStats (){
  if (Force2S && LastBurnStat != 3){                // Not if burning
    events.send(NTPStart,                           "SystemStart"); 
    events.send(ESP.getResetReason().c_str(),       "LastReset"); 
    events.send(String(ESP.getFreeHeap()).c_str (), "ESPHeap");
  }
}
void ResetDaily(){
  time_t  epochTime = timeClient.getEpochTime();
  struct tm * ptm; ptm = localtime (&epochTime);
  int monthDay = ptm->tm_mday;

  if (monthDay != USAGE.ActDay){
    Serial.println ("Dayly values reset.");
    USAGE.DayBurnL = 0.0;
    USAGE.DayBurnM = TimeChange * -1;
    USAGE.DayGenkW = 0.0;
    USAGE.DayWaitM = TimeChange * -1;
    USAGE.ActDay = monthDay;
    USAGE.Save();
  }
}
