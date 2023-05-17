
const char* HassSens = "homeassistant/sensor";
const char HASSColor[][7][32]= {
  // {"homeassistant/sensor/oilmeter/color_r/config",      "Color_R",              "oilmeter/sensor/0/R",   "OilMeter_Sensor0_R",      "RGB",         "gas", "measurement"},
  // HASS-NODE              NAME                    STATE-TOPIC     UNIQUE-ID         EAS-Unit        DEV-CLA  STATE-CLASS
    {"color_r/config",      "Color_R",              "sensor/0/R",   "Sensor0_R",      "RGB",         "gas", "measurement"},
    {"color_g/config",      "Color_G",              "sensor/0/G",   "Sensor0_G",      "RGB",         "gas", "measurement"}, 
    {"color_b/config",      "Color_B",              "sensor/0/B",   "Sensor0_B",      "RGB",         "gas", "measurement"}, 
    {"color_l/config",      "Lux",                  "sensor/0/L",   "Sensor0_L",      "RGB",         "gas", "measurement"}, 
    {"LastBurnM/config",    "Letze Brenndauer",     "LastBurnM",    "LastBurnM",      "Sec",         "gas", "measurement"}, 
    {"LastWaitM/config",    "Letze Wartedauer",     "LastWaitM",    "LastWaitM",      "Sec",         "gas", "measurement"}, 
    {"LastBurnL/config",    "Letze Brennmenge",     "LastBurnL",    "LastBurnL",      "L",           "gas", "measurement"}, 
    {"LastGenkW/config",    "Letze Generierung",    "LastGenkW",    "LastGenkW",      "kWh",         "gas", "measurement"}, 
    {"GesBurnM/config",     "Gesamte Brenndauer",   "GesBurnM",     "GesBurnM",       "Min",         "gas", "total_increasing"}, 
    {"GesWaitM/config",     "Gesamte Wartedauer",   "GesWaitM",     "GesWaitM",       "Min",         "gas", "total_increasing"}, 
    {"GesBurnL/config",     "Gesamte Brennmenge",   "GesBurnL",     "GesBurnL",       "L",           "gas", "total_increasing"}, 
    {"GesGenkW/config",     "Gesamte Generierung",  "GesGenkW",     "GesGenkW",       "kWh",         "gas", "total_increasing"}, 
    {"ActTankL/config",     "Tankinhalt_Aktuell",   "ActTankL",     "ActTankL",       "L",           "gas", "total_increasing"}, 
    {"MaxTankL/config",     "Tankinhalt_Maximal",   "MaxTankL",     "MaxTankL",       "kWh",         "gas", "measurement"},
    {"LastBurnStat/config", "Letzer Brennerstatus", "LastBurnStat", "LastBurnStat",    "",           "gas", "measurement"} 
};
const char HASSDistance[][7][32]= {
  // {"homeassistant/sensor/oilmeter/color_r/config",      "Color_R",              "oilmeter/sensor/0/R",   "OilMeter_Sensor0_R",      "RGB",         "gas", "measurement"},
  // HASS-NODE              NAME                    STATE-TOPIC             UNIQUE-ID         EAS-Unit        DEV-CLA  STATE-CLASS
    {"Dist/config",         "Distanz",              "LastSens",             "Sensor0_D",      "cm",          "water", "measurement"},
    {"ActTankL/config",     "Tankinhalt_Aktuell",   "ActTankL",             "ActTankL",       "L",           "water", "measurement"}, 
    {"MaxTankL/config",     "Tankinhalt_Maximal",   "MaxTankL",             "MaxTankL",       "L",           "water", "measurement"}
};

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
    Serial.println ("File loading success: Burn.json");
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
  int    ActDay    = 1;               // der aktuelle Bezugstag der Messung

  double LastWaitM  = 0.0;             // Letzter Wartezeit zwischen Starts
  double LastBurnM  = 0.0;             // Letzte Anzahl an Sekunden Brennvorgang
  double LastBurnL  = 0.0;             // Letzte Anzahl an Liter Brennvorgang
  double LastGenkW  = 0.0;             // Letzte Generierte kWh

  double DayWaitM  = 0.0;             // Letzter Wartezeit zwischen Starts
  double DayBurnM  = 0.0;             // Letzte Anzahl an Sekunden Brennvorgang
  double DayBurnL  = 0.0;             // Letzte Anzahl an Liter Brennvorgang
  double DayGenkW  = 0.0;             // Letzte Generierte kWh

  double GesWaitM  = 0.0;              // Gesamte Wartezeit des Brenners (incl. Vorheizen)
  double GesBurnM  = 0.0;              // Gesamte Anzahl an Sekunden Brennvorgang
  double GesBurnL  = 0.0;              // Gesamte Anzahl an Liter Brennvorgang
  double GesGenkW  = 0.0;              // Gesamte Generierte kWh

  double ActTankL  = 0.0;              // Tankinhalt aktuell in Liter

  bool Load () {
    DynamicJsonDocument JSON_DOC(512);
    bool LoadedUsage = LoadFile ("Usage.json", JSON_DOC);  
    if(LoadedUsage == false){Save();};
    ActDay    = JSON_DOC["ActDay"].as<int>();
    LastBurnL = JSON_DOC["LastBurnL"].as<double>();
    LastWaitM = JSON_DOC["LastWaitM"].as<double>();
    LastBurnM = JSON_DOC["LastBurnM"].as<double>();
    LastGenkW = JSON_DOC["LastGenkW"].as<double>();
    DayBurnL  = JSON_DOC["DayBurnL"].as<double>();
    DayWaitM  = JSON_DOC["DayWaitM"].as<double>();
    DayBurnM  = JSON_DOC["DayBurnM"].as<double>();
    DayGenkW  = JSON_DOC["DayGenkW"].as<double>();
    GesBurnL  = JSON_DOC["GesBurnL"].as<double>();
    GesWaitM  = JSON_DOC["GesWaitM"].as<double>();
    GesBurnM  = JSON_DOC["GesBurnM"].as<double>();
    GesGenkW  = JSON_DOC["GesGenkW"].as<double>();
    ActTankL  = JSON_DOC["ActTankL"].as<double>();
    Serial.println ("File loading success: Usage.json");
    return true;
  }
  bool Save (){
    Serial.println ("Save Usage.json");
    DynamicJsonDocument JSON_DOC(512); 
    JSON_DOC["ActDay"]     = ActDay;
    JSON_DOC["LastWaitM"]  = LastWaitM;
    JSON_DOC["LastBurnM"]  = LastBurnM;
    JSON_DOC["LastBurnL"]  = LastBurnL;
    JSON_DOC["LastGenkW"]  = LastGenkW;
    JSON_DOC["DayWaitM"]   = DayWaitM;
    JSON_DOC["DayBurnM"]   = DayBurnM;
    JSON_DOC["DayBurnL"]   = DayBurnL;
    JSON_DOC["DayGenkW"]   = DayGenkW;
    JSON_DOC["GesWaitM"]   = GesWaitM;
    JSON_DOC["GesBurnM"]   = GesBurnM;
    JSON_DOC["GesBurnL"]   = GesBurnL;
    JSON_DOC["GesGenkW"]   = GesGenkW;
    JSON_DOC["ActTankL"]   = ActTankL;

    char json_string[512];
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
    Serial.println ("File loading success: Wifi.json");
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
  char        TYPE[32] = "TCS34725"; // TCS34725, APDS9960, TRIGGER, SR04_SR04T, VL53L0X
  char        LIGHT[8] = "False";    // NUR TCS34725
  char        TRIGM[8] = "Zeit";     // Zeit = Berechnung nach Zeit
                                     // An = Fixabzug (L/H) bei Consumption wechsel auf true
                                     // Aus = Fixabzug (L/H) bei Consumption wechsel auf false
  int         GAIN     =  4;         // NUR TCS34725, APDS9960
  int         INTEG    =  50;        // NUR TCS34725, APDS9960
  int         HOHE     =  140;       // 140 cm ist das gefäss hoch
  int         ABST     =  16;        // Der Abstand von Sensor bis zum höchsten Punkt ist (cm)
  int         OBEN     =  3600;      // 60*60 cm = 3600 cm2
  int         UNTEN    =  2500;      // 50*50 cm = 2500 cm2

  bool Load () {
    DynamicJsonDocument JSON_DOC(256);
    bool Loaded = LoadFile ("Sens.json", JSON_DOC);  
    if(Loaded == false){Save();};
    strcpy( TYPE, JSON_DOC["TYPE"]);  
    strcpy( LIGHT, JSON_DOC["LIGHT"]);  
    if (JSON_DOC["TRIGM"].isNull()) {strcpy( TRIGM, "An");} else {strcpy( TRIGM, JSON_DOC["TRIGM"]);}; 
    GAIN = JSON_DOC["GAIN"];
    INTEG = JSON_DOC["INTEG"];
    OBEN = JSON_DOC["OBEN"];
    UNTEN = JSON_DOC["UNTEN"];
    ABST = JSON_DOC["ABST"];
    HOHE = JSON_DOC["HOHE"];
    Serial.println ("File loading success: Sens.json");
    return true;
  }
  bool Save (){
    DynamicJsonDocument JSON_DOC(256); 
    JSON_DOC["TYPE"]  = TYPE;
    JSON_DOC["GAIN"]  = GAIN;
    JSON_DOC["INTEG"] = INTEG;
    JSON_DOC["OBEN"]  = OBEN;
    JSON_DOC["UNTEN"] = UNTEN;
    JSON_DOC["ABST"]  = ABST;
    JSON_DOC["HOHE"] = HOHE;
    JSON_DOC["LIGHT"] = LIGHT;
    JSON_DOC["TRIGM"] = TRIGM;
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
    Serial.println ("File loading success: Mqtt.json");
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
