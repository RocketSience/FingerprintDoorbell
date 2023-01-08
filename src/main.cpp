/***************************************************
  Main of FingerprintDoorbell 
 ****************************************************/

#include <DNSServer.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#if defined(ESP32)
//#include "SPIFFS.h"
#include <LITTLEFS.h>
#define SPIFFS LittleFS  //replace spiffs
#include <WiFi.h>
#endif
#if defined(ESP8266)
//#include <FS.h>
#include <LITTLEFS.h>
#define SPIFFS LittleFS  //replace spiffs
#include <ESP8266wifi.h>
#endif
#include <PubSubClient.h>
#include "FingerprintManager.h"
#include "SettingsManager.h"
#include "global.h"

//KNX
#ifdef KNXFEATURE
#include "esp-knx-ip.h"
address_t door1_ga;
address_t door2_ga;
address_t message_ga;
address_t doorbell_ga;
address_t alarmdisable_ga;
address_t alarmarmed_ga;
address_t autounarm_ga;
address_t led_ga;
address_t ledstate_ga;
address_t touch_ga;
address_t touchstate_ga;
bool alarm_system_armed = false;
//String knx_router_ip = "192.168.0.199";

#endif

#if defined(ESP8266)
bool getLocalTime(struct tm * info, uint32_t ms = 5000)
{
    uint32_t start = millis();
    time_t now;
    while((millis()-start) <= ms) {
        time(&now);
        localtime_r(&now, info);
        if(info->tm_year > (2016 - 1900)){
            return true;
        }
        delay(10);
    }
    return false;
}
#endif

enum class Mode { scan, wait, enroll, wificonfig, maintenance };

const char* VersionInfo = "0.50";

// ===================================================================================================================
// Caution: below are not the credentials for connecting to your home network, they are for the Access Point mode!!!
// ===================================================================================================================
const char* WifiConfigSsid = "FingerprintDoorbell-Config"; // SSID used for WiFi when in Access Point mode for configuration
const char* WifiConfigPassword = "12345678"; // password used for WiFi when in Access Point mode for configuration. Min. 8 chars needed!
IPAddress   WifiConfigIp(192, 168, 4, 1); // IP of access point in wifi config mode

const char TIME_ZONE[] = "MEZ-1MESZ-2,M3.5.0/02:00:00,M10.5.0/03:00:00"; //MEZ MESZ Time

const int   templateSamples = 3; //Fingerprint Samples for Template
long rssi = 0.0;

//#define CUSTOM_GPIOS
#ifdef CUSTOM_GPIOS
  const int   customOutput1 = 18; // not used internally, but can be set over MQTT
  const int   customOutput2 = 26; // not used internally, but can be set over MQTT
  const int   customInput1 = 21; // not used internally, but changes are published over MQTT
  const int   customInput2 = 22; // not used internally, but changes are published over MQTT
  bool customInput1Value = false;
  bool customInput2Value = false;
  bool triggerCustomOutput1 = false;
  bool triggerCustomOutput2 = false;

  // Timer stuff Custom_GPIOS  
  const unsigned long customOutput1TriggerTime = 4 * 1000UL; //Trigger 4000ms
  const unsigned long customOutput2TriggerTime = 4 * 1000UL; 
#endif

// Timer stuff 
  const unsigned long rssiStatusIntervall = 1 * 15000UL; //Trigger every 15 Seconds
  const unsigned long doorBell_impulseDuration = 1 * 1000UL; 
  const unsigned long doorBell_blockAfterMatchDuration = 10 * 1000UL;   
  bool doorBell_blocked = false;
  bool doorBell_block_trigger = false;


  const unsigned long door1_impulseDuration = 2 * 1000UL; 
  const unsigned long door2_impulseDuration = 2 * 1000UL; 
  const unsigned long door1_triggerDelay = 1 * 1000UL; 
  const unsigned long door2_triggerDelay = 1 * 1000UL; 
  const unsigned long alarm_disable_impulseDuration = 1 * 1000UL; 
  const unsigned long wait_Duration = 2 * 1000UL;  
  bool doorBell_trigger = false; 
  bool door1_trigger = false; 
  bool door2_trigger = false;  
  bool door1_delayed_trigger = false;  
  bool door2_delayed_trigger = false; 
  bool alarm_disable_trigger = false;   

#ifdef DOORBELL_FEATURE
// Timer DoorBell
bool doorBell_trigger = false;
unsigned long prevDoorbellTime = 0;  
const unsigned long doorbellTriggerTime = 1 * 1000UL; //Trigger 1000ms
const int   doorbellOutputPin = 19; // pin connected to the doorbell (when using hardware connection instead of mqtt to ring the bell)
#endif

const int logMessagesCount = 10;
String logMessages[logMessagesCount]; // log messages, 0=most recent log message
bool shouldReboot = false;
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long mqttReconnectPreviousMillis = 0;

String enrollId;
String enrollName;
Mode currentMode = Mode::scan;

FingerprintManager fingerManager;
SettingsManager settingsManager;
bool needMaintenanceMode = false;

const byte DNS_PORT = 53;
DNSServer dnsServer;
AsyncWebServer webServer(80); // AsyncWebServer  on port 80
AsyncEventSource events("/events"); // event source (Server-Sent events)

WiFiClient espClient;

#ifdef MQTTFEATURE
PubSubClient mqttClient(espClient);

long lastMsg = 0;
char msg[50];
int value = 0;
bool mqttConfigValid = true;
#endif


Match lastMatch;

#ifdef KNXFEATURE

// Preferences
int loadRemanentIntFromPrefs(String key) {
  Preferences preferences;
  preferences.begin("Remanent", true); 
  int value = preferences.getInt(key.c_str(), -1);
  #ifdef DEBUG
  Serial.println((String)"loaded from Prefs: KEY:" + key + " VALUE:" + value);  
  #endif  
  preferences.end();
  return value;
}

void saveRemanentIntToPrefs(String key, int value) {
  Preferences preferences;
  preferences.begin("Remanent", false); 
  preferences.putInt(key.c_str(), value);
  #ifdef DEBUG
  Serial.println((String)"saved to Prefs: KEY:" + key + " VALUE:" + value);  
  #endif  
  preferences.end();  
}


void notifyKNX(String message) {  
  if (String(settingsManager.getKNXSettings().message_ga).isEmpty() == false){
      knx.write_14byte_string(message_ga, message.c_str());
      #ifdef DEBUG
        Serial.print("KNX Message: ");
        Serial.println(message);
      #endif
}else{
  #ifdef DEBUG
        Serial.print("KNX Message (no GA): ");
        Serial.println(message);
      #endif
}
}

void ledStateToKNX(){
  static int lastLedState = -1;
  if (fingerManager.LedTouchRing != lastLedState){
    lastLedState = fingerManager.LedTouchRing;
    if (String(settingsManager.getKNXSettings().ledstate_ga).isEmpty() == false){    
    knx.write_1bit(ledstate_ga, lastLedState);
    }
    }
}

void touchStateToKNX(){
  static int lastTouchState = -1;
  if (fingerManager.ignoreTouchRing != lastTouchState){
    lastTouchState = fingerManager.ignoreTouchRing;
    if (String(settingsManager.getKNXSettings().touchstate_ga).isEmpty() == false){
      if (lastTouchState == 1){
        knx.write_1bit(touchstate_ga, false);        
      }else{
        knx.write_1bit(touchstate_ga, true);        
      }    
    }
  }
}

void led_cb(message_t const &msg, void *arg)
{
	//switch (ct)
	switch (msg.ct)
	{
	case KNX_CT_WRITE:
		if (msg.data[0] == 1){
      fingerManager.setLedTouchRing(true);
      saveRemanentIntToPrefs ("led", 1);
      notifyKNX(String("LED_ON"));
    }
    else if (msg.data[0] == 0){
      fingerManager.setLedTouchRing(false);
      saveRemanentIntToPrefs ("led", 0);
      notifyKNX(String("LED_OFF"));
    }
    
    #ifdef DEBUG
        Serial.println("LED Write Callback triggered!");
        Serial.print("Value: ");
        Serial.println(msg.data[0]);
    #endif    
		break;
	case KNX_CT_READ:
    #ifdef DEBUG
        Serial.println("LED Read Callback triggered!");
    #endif
		    if (String(settingsManager.getKNXSettings().ledstate_ga).isEmpty() == false){
        knx.answer_1bit(ledstate_ga, fingerManager.LedTouchRing);
        }
        Serial.println((String)"LED State: " + fingerManager.LedTouchRing);    
		break;
	}
}

void touch_cb(message_t const &msg, void *arg)
{
	//switch (ct)
	switch (msg.ct)
	{
	case KNX_CT_WRITE:
		if (msg.data[0] == 1){
      fingerManager.setIgnoreTouchRing(false);
      saveRemanentIntToPrefs ("touch_on", 1);      
      notifyKNX(String("TOUCH_ON"));      
    }
    else if (msg.data[0] == 0){
      fingerManager.setIgnoreTouchRing(true);
      saveRemanentIntToPrefs ("touch_on", 0); 
      notifyKNX(String("TOUCH_OFF"));
    }
    #ifdef DEBUG
        Serial.println("Touch Write Callback triggered!");
        Serial.print("Value: ");
        Serial.println(msg.data[0]);
    #endif    
		break;
	case KNX_CT_READ:
    #ifdef DEBUG
        Serial.println("Touch Read Callback triggered!");
    #endif
		  if (String(settingsManager.getKNXSettings().touchstate_ga).isEmpty() == false){        
        if (fingerManager.ignoreTouchRing == 1){
          knx.answer_1bit(touchstate_ga, false);
          Serial.println((String)"Touch State: OFF");
        }else{
          knx.answer_1bit(touchstate_ga, true);
          Serial.println((String)"Touch State: ON");
        }  
      }  
		break;
	}
}

void alarm_armed_cb(message_t const &msg, void *arg)
{
	//switch (ct)
	switch (msg.ct)
	{
	case KNX_CT_WRITE:
		if (msg.data[0] == 1){
      alarm_system_armed = true;
      saveRemanentIntToPrefs ("alarm_armed", 1);      
    }
    else if (msg.data[0] == 0){
      alarm_system_armed = false;
      saveRemanentIntToPrefs ("alarm_armed", 0);      
    }    
    #ifdef DEBUG
        Serial.println("Alarm Status Callback triggered!");
        Serial.print("Value: ");
        Serial.println(msg.data[0]);

    #endif    
		break;
	case KNX_CT_READ:
    #ifdef DEBUG
        Serial.println("Alarm Status Read Callback triggered!");
    #endif		
		break;
	}
}

int getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found>index ? data.substring(strIndex[0], strIndex[1]).toInt(): -1;
}

bool isNumberInList(String data, char separator, int number)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
        if (data.substring(strIndex[0], strIndex[1]).toInt()==number){
          return true;
        }
    }
  }
  return false;
}



void SetupKNX(){			
  
  KNXSettings knxSettings = settingsManager.getKNXSettings();
    #ifdef DEBUG
    Serial.print("KNX PA: ");
    Serial.println(knxSettings.knx_pa.c_str());
    Serial.print("KNX Router IP: ");
    Serial.println(knxSettings.knxrouter_ip.c_str());         
    Serial.print("KNX Door1 GA: ");
    Serial.println(knxSettings.door1_ga.c_str());    
    Serial.print("KNX Door2 GA: ");
    Serial.println(knxSettings.door2_ga.c_str());
    Serial.print("KNX DoorBell GA: ");
    Serial.println(knxSettings.doorbell_ga.c_str());
    Serial.print("KNX Alarm Disable GA: ");
    Serial.println(knxSettings.alarmdisable_ga.c_str());
    Serial.print("KNX Alarm is armed GA: ");
    Serial.println(knxSettings.alarmarmed_ga.c_str());
    Serial.print("KNX Alarm automatic undarmed GA: ");
    Serial.println(knxSettings.autounarm_ga.c_str());
    Serial.print("KNX LED-Ring ON/OFF GA: ");
    Serial.println(knxSettings.led_ga.c_str());
    Serial.print("KNX LED-Ring ON/OFF State GA: ");
    Serial.println(knxSettings.ledstate_ga.c_str());
    Serial.print("KNX Ignore Touch GA: ");
    Serial.println(knxSettings.touch_ga.c_str());
    Serial.print("KNX Ignore Touch State GA: ");
    Serial.println(knxSettings.touchstate_ga.c_str());
    Serial.print("KNX Message / Status GA: ");
    Serial.println(knxSettings.message_ga.c_str());
    #endif

  /*knx.physical_address_set(knx.PA_to_address(
    getValue(knxSettings.knx_pa,'.',0), 
    getValue(knxSettings.knx_pa,'.',1), 
    getValue(knxSettings.knx_pa,'.',2)));   
  */

  door1_ga = knx.GA_to_address(
    getValue(knxSettings.door1_ga,'/',0), 
    getValue(knxSettings.door1_ga,'/',1), 
    getValue(knxSettings.door1_ga,'/',2));

  door2_ga = knx.GA_to_address(
    getValue(knxSettings.door2_ga,'/',0), 
    getValue(knxSettings.door2_ga,'/',1), 
    getValue(knxSettings.door2_ga,'/',2));  

  doorbell_ga = knx.GA_to_address(
    getValue(knxSettings.doorbell_ga,'/',0), 
    getValue(knxSettings.doorbell_ga,'/',1), 
    getValue(knxSettings.doorbell_ga,'/',2)); 

  alarmdisable_ga = knx.GA_to_address(
    getValue(knxSettings.alarmdisable_ga,'/',0), 
    getValue(knxSettings.alarmdisable_ga,'/',1), 
    getValue(knxSettings.alarmdisable_ga,'/',2)); 

  alarmarmed_ga = knx.GA_to_address(
    getValue(knxSettings.alarmarmed_ga,'/',0), 
    getValue(knxSettings.alarmarmed_ga,'/',1), 
    getValue(knxSettings.alarmarmed_ga,'/',2)); 

  autounarm_ga = knx.GA_to_address(
    getValue(knxSettings.autounarm_ga,'/',0), 
    getValue(knxSettings.autounarm_ga,'/',1), 
    getValue(knxSettings.autounarm_ga,'/',2)); 

  message_ga = knx.GA_to_address(
    getValue(knxSettings.message_ga,'/',0), 
    getValue(knxSettings.message_ga,'/',1), 
    getValue(knxSettings.message_ga,'/',2)); 

  led_ga = knx.GA_to_address(
    getValue(knxSettings.led_ga,'/',0), 
    getValue(knxSettings.led_ga,'/',1), 
    getValue(knxSettings.led_ga,'/',2)); 

  ledstate_ga = knx.GA_to_address(
    getValue(knxSettings.ledstate_ga,'/',0), 
    getValue(knxSettings.ledstate_ga,'/',1), 
    getValue(knxSettings.ledstate_ga,'/',2)); 

  touch_ga = knx.GA_to_address(
    getValue(knxSettings.touch_ga,'/',0), 
    getValue(knxSettings.touch_ga,'/',1), 
    getValue(knxSettings.touch_ga,'/',2)); 

  touchstate_ga = knx.GA_to_address(
    getValue(knxSettings.touchstate_ga,'/',0), 
    getValue(knxSettings.touchstate_ga,'/',1), 
    getValue(knxSettings.touchstate_ga,'/',2)); 

  callback_id_t set_LED_id = knx.callback_register("Set LED Ring on/off", led_cb);
  callback_id_t get_LEDSTATE_id = knx.callback_register("Get LED Ring State on/off", led_cb);
  callback_id_t set_TOUCH_id = knx.callback_register("Set Touch Ignore on/off", touch_cb);  
  callback_id_t get_TOUCHSTATE_id = knx.callback_register("Get Touch Ignore State on/off", touch_cb);  
  callback_id_t set_ALARM_ARMED_id = knx.callback_register("Status from Alarm System to FingerPrint", alarm_armed_cb);     

  // Assign callbacks to group addresses  
  knx.callback_assign(set_LED_id, knx.GA_to_address(
     getValue(knxSettings.led_ga,'/',0), 
     getValue(knxSettings.led_ga,'/',1), 
     getValue(knxSettings.led_ga,'/',2))); 

  knx.callback_assign(get_LEDSTATE_id, knx.GA_to_address(
     getValue(knxSettings.ledstate_ga,'/',0), 
     getValue(knxSettings.ledstate_ga,'/',1), 
     getValue(knxSettings.ledstate_ga,'/',2))); 

  knx.callback_assign(set_TOUCH_id, knx.GA_to_address(
     getValue(knxSettings.touch_ga,'/',0), 
     getValue(knxSettings.touch_ga,'/',1), 
     getValue(knxSettings.touch_ga,'/',2))); 

  knx.callback_assign(get_TOUCHSTATE_id, knx.GA_to_address(
     getValue(knxSettings.touchstate_ga,'/',0), 
     getValue(knxSettings.touchstate_ga,'/',1), 
     getValue(knxSettings.touchstate_ga,'/',2))); 

  knx.callback_assign(set_ALARM_ARMED_id, knx.GA_to_address(
     getValue(knxSettings.alarmarmed_ga,'/',0), 
     getValue(knxSettings.alarmarmed_ga,'/',1), 
     getValue(knxSettings.alarmarmed_ga,'/',2))); 

    //knx.udpAddress_set(knx_router_ip.c_str());
    knx.udpAddress_set(knxSettings.knxrouter_ip.c_str());
}
#endif


void addLogMessage(const String& message) {
  // shift all messages in array by 1, oldest message will die
  for (int i=logMessagesCount-1; i>0; i--)
    logMessages[i]=logMessages[i-1];
  logMessages[0]=message;
}

String getLogMessagesAsHtml() {
  String html = "";
  for (int i=logMessagesCount-1; i>=0; i--) {
    if (logMessages[i]!="")
      html = html + logMessages[i] + "<br>";
  }
  return html;
}

String getTimestampString(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    #ifdef DEBUG
    Serial.println("Failed to obtain time");
    #endif
    return "no time";
  }
  
  char buffer[25];
  strftime(buffer,sizeof(buffer),"%Y-%m-%d %H:%M:%S %Z", &timeinfo);
  String datetime = String(buffer);
  return datetime;
}

/* wait for maintenance mode or timeout 5s */
bool waitForMaintenanceMode() {
  needMaintenanceMode = true;
  unsigned long startMillis = millis();
  while (currentMode != Mode::maintenance) {
    if ((millis() - startMillis) >= 5000ul) {
      needMaintenanceMode = false;
      return false;
    }
    delay(50);
  }
  needMaintenanceMode = false;
  return true;
}

// Replaces placeholder in HTML pages
String processor(const String& var){
  if(var == "LOGMESSAGES"){
    return getLogMessagesAsHtml();
  } else if (var == "FINGERLIST") {
    return fingerManager.getFingerListAsHtmlOptionList();
  } else if (var == "HOSTNAME") {
    return settingsManager.getWifiSettings().hostname;
  } else if (var == "VERSIONINFO") {
    return VersionInfo;
  } else if (var == "RSSIINFO") {
    char rssistr[20];
    sprintf(rssistr,"%ld",rssi);
    return rssistr;
  } else if (var == "TP_SCANS") {    
    char tpscans[5];
    sprintf(tpscans,"%i",settingsManager.getAppSettings().tpScans);
    return tpscans;
  } else if (var == "WIFI_SSID") {
    return settingsManager.getWifiSettings().ssid;
  } else if (var == "WIFI_PASSWORD") {
    if (settingsManager.getWifiSettings().password.isEmpty())
      return "";
    else
      return "********"; // for security reasons the wifi password will not left the device once configured
  } else if (var == "MQTT_SERVER") {
    return settingsManager.getAppSettings().mqttServer;
  } else if (var == "MQTT_USERNAME") {
    return settingsManager.getAppSettings().mqttUsername;
  } else if (var == "MQTT_PASSWORD") {
    return settingsManager.getAppSettings().mqttPassword;
  } else if (var == "MQTT_ROOTTOPIC") {
    return settingsManager.getAppSettings().mqttRootTopic;
  } else if (var == "NTP_SERVER") {
    return settingsManager.getAppSettings().ntpServer;

    } else if (var == "KNX_PA") {
    return settingsManager.getKNXSettings().knx_pa;
    } else if (var == "KNXROUTER_IP") {
    return settingsManager.getKNXSettings().knxrouter_ip;
    } else if (var == "DOOR1_GA") {
    return settingsManager.getKNXSettings().door1_ga;
    } else if (var == "DOOR2_GA") {
    return settingsManager.getKNXSettings().door2_ga;
    } else if (var == "DOORBELL_GA") {
    return settingsManager.getKNXSettings().doorbell_ga;
    } else if (var == "ALARMDISABLE_GA") {
    return settingsManager.getKNXSettings().alarmdisable_ga;
    } else if (var == "ALARMARMED_GA") {
    return settingsManager.getKNXSettings().alarmarmed_ga;
    } else if (var == "AUTOUNARM_GA") {
    return settingsManager.getKNXSettings().autounarm_ga;
    } else if (var == "MESSAGE_GA") {
    return settingsManager.getKNXSettings().message_ga;
    } else if (var == "LED_GA") {
    return settingsManager.getKNXSettings().led_ga;
    } else if (var == "LEDSTATE_GA") {
    return settingsManager.getKNXSettings().ledstate_ga;
    } else if (var == "TOUCH_GA") {
    return settingsManager.getKNXSettings().touch_ga;
    } else if (var == "TOUCHSTATE_GA") {
    return settingsManager.getKNXSettings().touchstate_ga;
    } else if (var == "DOOR1_LIST") {
    return settingsManager.getKNXSettings().door1_list;
    } else if (var == "DOOR2_LIST") {
    return settingsManager.getKNXSettings().door2_list;
  }

  return String();
}


// send LastMessage to websocket clients
void notifyClients(String message) {
  String messageWithTimestamp = "[" + getTimestampString() + "]: " + message;
  #ifdef DEBUG
  Serial.println(messageWithTimestamp);
  #endif
  addLogMessage(messageWithTimestamp);
  events.send(getLogMessagesAsHtml().c_str(),"message",millis(),1000);
  #ifdef MQTTFEATURE
  String mqttRootTopic = settingsManager.getAppSettings().mqttRootTopic;
  mqttClient.publish((String(mqttRootTopic) + "/lastLogMessage").c_str(), message.c_str());
  #endif
}



void updateClientsFingerlist(String fingerlist) {
  #ifdef DEBUG
  Serial.println("New fingerlist was sent to clients");
  #endif
  events.send(fingerlist.c_str(),"fingerlist",millis(),1000);
}


bool doPairing() {
  String newPairingCode = settingsManager.generateNewPairingCode();

  if (fingerManager.setPairingCode(newPairingCode)) {
    AppSettings settings = settingsManager.getAppSettings();
    settings.sensorPairingCode = newPairingCode;
    settings.sensorPairingValid = true;
    settingsManager.saveAppSettings(settings);
    notifyClients("Pairing successful.");
    return true;
  } else {
    notifyClients("Pairing failed.");
    return false;
  }

}


bool checkPairingValid() {
  AppSettings settings = settingsManager.getAppSettings();

   if (!settings.sensorPairingValid) {
     if (settings.sensorPairingCode.isEmpty()) {
       // first boot, do pairing automatically so the user does not have to do this manually
       return doPairing();
     } else {
      #ifdef DEBUG
      Serial.println("Pairing has been invalidated previously.");   
      #endif
      return false;
     }
   }

  String actualSensorPairingCode = fingerManager.getPairingCode();
  //Serial.println("Awaited pairing code: " + settings.sensorPairingCode);
  //Serial.println("Actual pairing code: " + actualSensorPairingCode);

  if (actualSensorPairingCode.equals(settings.sensorPairingCode))
    return true;
  else {
    if (!actualSensorPairingCode.isEmpty()) { 
      // An empty code means there was a communication problem. So we don't have a valid code, but maybe next read will succeed and we get one again.
      // But here we just got an non-empty pairing code that was different to the awaited one. So don't expect that will change in future until repairing was done.
      // -> invalidate pairing for security reasons
      AppSettings settings = settingsManager.getAppSettings();
      settings.sensorPairingValid = false;
      settingsManager.saveAppSettings(settings);
    }
    return false;
  }
}


bool initWifi() {
  // Connect to Wi-Fi
  WifiSettings wifiSettings = settingsManager.getWifiSettings();
  WiFi.mode(WIFI_STA);
  //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
#if defined(ESP32)
  WiFi.setHostname(wifiSettings.hostname.c_str()); //define hostname
#endif
 #if defined(ESP8266)
  WiFi.hostname(wifiSettings.hostname.c_str()); //define hostname  
 #endif  
  
  WiFi.begin(wifiSettings.ssid.c_str(), wifiSettings.password.c_str());
    #ifdef DEBUG   
    Serial.print("SSID: ");
    Serial.println(wifiSettings.ssid.c_str());    
    Serial.print("HOSTNAME: ");
    Serial.println(wifiSettings.hostname.c_str());
    #endif
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    #ifdef DEBUG   
    Serial.println("Waiting for WiFi connection...");    
    #endif
    counter++;
    if (counter > 30)
      return false;
  }
  #ifdef DEBUG
  Serial.println("Connected!");
  #endif

  // Print ESP32 Local IP Address
  #ifdef DEBUG
  Serial.println(WiFi.localIP());
  #endif  
  return true;
}

void initWiFiAccessPointForConfiguration() {
  WiFi.softAPConfig(WifiConfigIp, WifiConfigIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(WifiConfigSsid, WifiConfigPassword);

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.start(DNS_PORT, "*", WifiConfigIp);

  #ifdef DEBUG
  Serial.print("AP IP address: ");
  Serial.println(WifiConfigIp); 
  #endif
}


void startWebserver(){
  
  // Initialize SPIFFS
  if(!SPIFFS.begin()){
    #ifdef DEBUG
    Serial.println("An Error has occurred while mounting SPIFFS");
    #endif    
    //return;
  }

  // Init time by NTP Client  
  String ntpServer = settingsManager.getAppSettings().ntpServer;
  #ifdef DEBUG
    Serial.print("NTP Server: ");
    Serial.println(ntpServer);    
  #endif  
  configTzTime(TIME_ZONE, ntpServer.c_str());   

  // webserver for normal operating or wifi config?
  if (currentMode == Mode::wificonfig)
  {
    // =================
    // WiFi config mode
    // =================

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/wificonfig.html", String(), false, processor);
    });

    webServer.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("hostname"))
      {
        #ifdef DEBUG
        Serial.println("Save wifi config");
        #endif
        WifiSettings settings = settingsManager.getWifiSettings();
        settings.hostname = request->arg("hostname");
        settings.ssid = request->arg("ssid");
        if (request->arg("password").equals("********")) // password is replaced by wildcards when given to the browser, so if the user didn't changed it, don't save it
          settings.password = settingsManager.getWifiSettings().password; // use the old, already saved, one
        else
          settings.password = request->arg("password");
        settingsManager.saveWifiSettings(settings);
        shouldReboot = true;
      }
      request->redirect("/");
    });


    webServer.onNotFound([](AsyncWebServerRequest *request){
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->printf("<!DOCTYPE html><html><head><title>FingerprintDoorbell</title><meta http-equiv=\"refresh\" content=\"0; url=http://%s\" /></head><body>", WiFi.softAPIP().toString().c_str());
      response->printf("<p>Please configure your WiFi settings <a href='http://%s'>here</a> to connect FingerprintDoorbell to your home network.</p>", WiFi.softAPIP().toString().c_str());
      response->print("</body></html>");
      request->send(response);
    });

  }
  else
  {
    // =======================
    // normal operating mode
    // =======================
    events.onConnect([](AsyncEventSourceClient *client){
      if(client->lastId()){
        #ifdef DEBUG
        Serial.printf("Client reconnected! Last message ID it got was: %u\n", client->lastId());
        #endif
      }
      //send event with message "ready", id current millis
      // and set reconnect delay to 1 second
      client->send(getLogMessagesAsHtml().c_str(),"message",millis(),1000);
    });
    webServer.addHandler(&events);

    
    // Route for root / web page
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/index.html", String(), false, processor);      
    });

    webServer.on("/enroll", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("startEnrollment"))
      {
        enrollId = request->arg("newFingerprintId");
        enrollName = request->arg("newFingerprintName");
        currentMode = Mode::enroll;
      }
      request->redirect("/");
    });

    webServer.on("/editFingerprints", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("selectedFingerprint"))
      {
        if(request->hasArg("btnDelete"))
        {
          int id = request->arg("selectedFingerprint").toInt();
          waitForMaintenanceMode();
          fingerManager.deleteFinger(id);
          currentMode = Mode::scan;
        }
        else if (request->hasArg("btnRename"))
        {
          int id = request->arg("selectedFingerprint").toInt();
          String newName = request->arg("renameNewName");
          fingerManager.renameFinger(id, newName);
        }
      }
      request->redirect("/");  
    });

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("btnSaveSettings"))
      {
        #ifdef DEBUG
        Serial.println("Save settings");
        #endif
        AppSettings settings = settingsManager.getAppSettings();
        settings.tpScans = (uint16_t) request->arg("tp_scans").toInt();
        settings.mqttServer = request->arg("mqtt_server");
        settings.mqttUsername = request->arg("mqtt_username");
        settings.mqttPassword = request->arg("mqtt_password");
        settings.mqttRootTopic = request->arg("mqtt_rootTopic");
        settings.ntpServer = request->arg("ntpServer");
        settingsManager.saveAppSettings(settings);
        request->redirect("/");  
        shouldReboot = true;
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      }
    });

    webServer.on("/knx", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("btnSaveSettings"))
      {
        #ifdef DEBUG
        Serial.println("Save settings");
        #endif
        KNXSettings settings = settingsManager.getKNXSettings();        
        settings.door1_ga = request->arg("door1_ga");
        settings.door2_ga = request->arg("door2_ga");
        settings.doorbell_ga = request->arg("doorbell_ga");
        settings.alarmdisable_ga = request->arg("alarmdisable_ga");
        settings.alarmarmed_ga = request->arg("alarmarmed_ga");
        settings.autounarm_ga = request->arg("autounarm_ga");
        settings.led_ga = request->arg("led_ga");
        settings.ledstate_ga = request->arg("ledstate_ga");
        settings.touch_ga = request->arg("touch_ga");        
        settings.touchstate_ga = request->arg("touchstate_ga");        
        settings.message_ga = request->arg("message_ga");
        settings.knx_pa = request->arg("knx_pa");
        settings.knxrouter_ip = request->arg("knxrouter_ip");
        settings.door1_list = request->arg("door1_list");
        settings.door2_list = request->arg("door2_list");
        settingsManager.saveKNXSettings(settings);
        request->redirect("/");  
        shouldReboot = true;
      } else {
        request->send(SPIFFS, "/knx.html", String(), false, processor);
      }
    });

    webServer.on("/pairing", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("btnDoPairing"))
      {
        #ifdef DEBUG
        Serial.println("Do (re)pairing");
        #endif
        doPairing();
        request->redirect("/");  
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      }
    });



    webServer.on("/factoryReset", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("btnFactoryReset"))
      {
        notifyClients("Factory reset initiated...");
        
        if (!fingerManager.deleteAll())
          notifyClients("Finger database could not be deleted.");
        
        if (!settingsManager.deleteAppSettings())
          notifyClients("App settings could not be deleted.");

        if (!settingsManager.deleteWifiSettings())
          notifyClients("Wifi settings could not be deleted.");
        
        request->redirect("/");  
        shouldReboot = true;
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      }
    });


    webServer.on("/deleteAllFingerprints", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasArg("btnDeleteAllFingerprints"))
      {
        notifyClients("Deleting all fingerprints...");
        
        if (!fingerManager.deleteAll())
          notifyClients("Finger database could not be deleted.");
        
        request->redirect("/");  
        
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      }
    });


    webServer.onNotFound([](AsyncWebServerRequest *request){
      request->send(404);
    });

    
  } // end normal operating mode


  // common url callbacks
  webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/");
    shouldReboot = true;
  });

  webServer.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/bootstrap.min.css", "text/css");
  });


  // Enable Over-the-air updates at http://<IPAddress>/update
  AsyncElegantOTA.begin(&webServer);
  
  // Start server
  webServer.begin();

  notifyClients("System booted successfully!");

}


#ifdef MQTTFEATURE
void mqttCallback(char* topic, byte* message, unsigned int length) {
  #ifdef DEBUG
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  #endif
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    #ifdef DEBUG
    Serial.print((char)message[i]);
    #endif
    messageTemp += (char)message[i];
  }
  #ifdef DEBUG
  Serial.println();
  #endif

  // Check incomming message for interesting topics
  if (String(topic) == String(settingsManager.getAppSettings().mqttRootTopic) + "/ignoreTouchRing") {
    if(messageTemp == "on"){
      fingerManager.setIgnoreTouchRing(true);
    }
    else if(messageTemp == "off"){
      fingerManager.setIgnoreTouchRing(false);
    }
  }

  if (String(topic) == String(settingsManager.getAppSettings().mqttRootTopic) + "/LedTouchRing") {
    if(messageTemp == "on"){
      fingerManager.setLedTouchRing(true);
    }
    else if(messageTemp == "off"){
      fingerManager.setLedTouchRing(false);
    }
  }

  #ifdef CUSTOM_GPIOS
    if (String(topic) == String(settingsManager.getAppSettings().mqttRootTopic) + "/customOutput1") {
      if(messageTemp == "on"){
        triggerCustomOutput1 = true;
        //digitalWrite(customOutput1, HIGH);         
      }
      else if(messageTemp == "off"){
        digitalWrite(customOutput1, LOW); 
      }
    }
    if (String(topic) == String(settingsManager.getAppSettings().mqttRootTopic) + "/customOutput2") {
      if(messageTemp == "on"){
        triggerCustomOutput2 = true;
        //digitalWrite(customOutput2, HIGH); 
      }
      else if(messageTemp == "off"){
        digitalWrite(customOutput2, LOW); 
      }
    }
  #endif  

}
#endif

#ifdef CUSTOM_GPIOS
static unsigned long prevCustomOutput1Time = 0;
static unsigned long prevCustomOutput2Time = 0;
void doCustomOutputs(){
  if (triggerCustomOutput1 == true){
    triggerCustomOutput1 = false;
    prevCustomOutput1Time = millis(); 
    digitalWrite(customOutput1, HIGH);    
  }
  if (triggerCustomOutput2 == true){
    triggerCustomOutput2 = false;
    prevCustomOutput2Time = millis(); 
    digitalWrite(customOutput2, HIGH);    
  }
  
  if (digitalRead(customOutput1) == true && (millis() - prevCustomOutput1Time >= customOutput1TriggerTime))
	{		
    digitalWrite(customOutput1, LOW);
  }
  if (digitalRead(customOutput2) == true && (millis() - prevCustomOutput2Time >= customOutput2TriggerTime))
	{	
    digitalWrite(customOutput2, LOW);
  }  
}
#endif

void doWait(unsigned long duration){  
  static bool active = false;
  static unsigned long startTime = 0;
 if (active == false){
  active = true;  
  startTime = millis();  
 }else if ((active == true) && (millis() - startTime >= duration)){
  active = false;  
  currentMode = Mode::scan;
}
}

void doDoorbellBlock(){  
  static bool active = false;
  static unsigned long startTime = 0;
 if ((doorBell_block_trigger == true)){
  doorBell_block_trigger = false;
  doorBell_blocked = true;
  active = true;
  #ifdef DEBUG
        Serial.println("doorbell_blocked!");
  #endif    
  startTime = millis();  
 }else if ((active == true) && (millis() - startTime >= doorBell_blockAfterMatchDuration)){
    #ifdef DEBUG
        Serial.println("doorbell_unblocked!");
      #endif
  doorBell_blocked = false;
  active = false;    
}
}


void doDoor1TriggerDelay(){  
  static bool active = false;
  static unsigned long startTime = 0;
 if ((door1_delayed_trigger == true)){
  door1_delayed_trigger = false;  
  active = true;
  #ifdef DEBUG
        Serial.println("door1_trigger_delay_started!");
  #endif    
  startTime = millis();  
 }else if ((active == true) && (millis() - startTime >= door1_triggerDelay)){
    #ifdef DEBUG
        Serial.println("door1_delayed_trigger_now!");
      #endif
  door1_trigger = true;
  active = false;    
}
}

void doDoor2TriggerDelay(){  
  static bool active = false;
  static unsigned long startTime = 0;
 if ((door2_delayed_trigger == true)){
  door2_delayed_trigger = false;  
  active = true;
  #ifdef DEBUG
        Serial.println("door2_trigger_delay_started!");
  #endif    
  startTime = millis();  
 }else if ((active == true) && (millis() - startTime >= door2_triggerDelay)){
    #ifdef DEBUG
        Serial.println("door2_delayed_trigger_now!");
      #endif
  door2_trigger = true;
  active = false;    
}
}

void doDoorbell(){  
  String mqttRootTopic = settingsManager.getAppSettings().mqttRootTopic;
  static bool active = false;
  static unsigned long startTime = 0;
  if ((doorBell_trigger == true) && (doorBell_blocked == true))
  doorBell_trigger = false;
  else if ((doorBell_trigger == true) && (doorBell_blocked == false)){  
    active = true;    
    doorBell_trigger = false;
    startTime = millis();            
    #ifdef MQTTFEATURE
    mqttClient.publish((String(mqttRootTopic) + "/ring").c_str(), "on");
    #endif
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().doorbell_ga).isEmpty() == false){
        knx.write_1bit(doorbell_ga, 1);
        notifyKNX( String("No Match: ring"));
      
        #ifdef DEBUG
          Serial.println("doorbell_triggered!");
        #endif
      }else{
        #ifdef DEBUG
          Serial.println("doorbell_triggered_no_GA!");
        #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(doorbellOutputPin, HIGH);    
    #endif
  }else if ((active == true) && (millis() - startTime >= doorBell_impulseDuration))
	{		
    active = false;
    #ifdef MQTTFEATURE
      mqttClient.publish((String(mqttRootTopic) + "/ring").c_str(), "off");
    #endif
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().doorbell_ga).isEmpty() == false){
        knx.write_1bit(doorbell_ga, 0);
        #ifdef DEBUG
          Serial.println("doorBell_triggered_end!");
        #endif
      }else{
        #ifdef DEBUG
          Serial.println("doorBell_triggered_end_no_GA!");
        #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(doorbellOutputPin, LOW);
    #endif
  } 
}

  #ifdef KNXFEATURE
  void doDoor1(){  
  static bool active = false;
  static unsigned long startTime = 0;
  if (door1_trigger == true){
    active = true;    
    door1_trigger = false;
    startTime = millis();            
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().door1_ga).isEmpty() == false){
      knx.write_1bit(door1_ga, 1);
      #ifdef DEBUG
        Serial.println("door1_triggered!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("door1_triggered_no_GA!");
      #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(customOutput1, HIGH);    
    #endif
  }else if ((active == true) && (millis() - startTime >= door1_impulseDuration))
	{		
    active = false;
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().door1_ga).isEmpty() == false){
      knx.write_1bit(door1_ga, 0);
      #ifdef DEBUG
        Serial.println("door1_triggered_end!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("door1_triggered_end_no_GA!");
      #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(customOutput1, LOW);
    #endif
  }  
}
#endif

#ifdef KNXFEATURE
void doDoor2(){  
  static bool active = false;
  static unsigned long startTime = 0;
  if (door2_trigger == true){
    active = true;    
    door2_trigger = false;
    startTime = millis();            
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().door2_ga).isEmpty() == false){
      knx.write_1bit(door2_ga, 1);
      #ifdef DEBUG
        Serial.println("door2_triggered!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("door2_triggered_no_GA!");
      #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(customOutput2, HIGH);    
    #endif
  }else if ((active == true) && (millis() - startTime >= door2_impulseDuration))
	{		
    active = false;
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().door2_ga).isEmpty() == false){
      knx.write_1bit(door2_ga, 0);
      #ifdef DEBUG
        Serial.println("door2_triggered_end!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("door2_triggered_end_no_GA!");
      #endif
      }
    #endif
    #ifdef CUSTOM_GPIOS
      digitalWrite(customOutput2, LOW);
    #endif
  }  
}
#endif

#ifdef KNXFEATURE
void doAlarmDisable(){  
  static bool active = false;
  static unsigned long startTime = 0;
  if (alarm_disable_trigger == true){
    active = true;    
    alarm_disable_trigger = false;
    startTime = millis();            
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().alarmdisable_ga).isEmpty() == false){
      knx.write_1bit(alarmdisable_ga, 0);
      #ifdef DEBUG
        Serial.println("alarm_disable_triggered!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("alarm_disable_triggered_no_GA!");
      #endif
      }
    #endif    
  }else if ((active == true) && (millis() - startTime >= alarm_disable_impulseDuration))
	{		
    active = false;
    #ifdef KNXFEATURE
      if (String(settingsManager.getKNXSettings().alarmdisable_ga).isEmpty() == false){
      knx.write_1bit(alarmdisable_ga, 0);
      #ifdef DEBUG
        Serial.println("alarm_disable_repeated!");
      #endif
      }else{
        #ifdef DEBUG
        Serial.println("alarm_disable_repeated_no_GA!");
      #endif
      }
      if ((alarm_system_armed == true) && (String(settingsManager.getKNXSettings().autounarm_ga).isEmpty() == false)){
      knx.write_1bit(autounarm_ga, 1);
      #ifdef DEBUG
        Serial.println("alarm_autounarm_Message_triggered!");
      #endif
      }
    #endif      
}
}
#endif

void doRssiStatus(){
  static unsigned long startTime = 0;
  static unsigned long prevRssiStatusTime = 0;  
  // send RSSI Value over MQTT every n seconds
  unsigned long now = millis();    
    if (now - prevRssiStatusTime >= rssiStatusIntervall) {      
      prevRssiStatusTime = now;
      rssi = WiFi.RSSI();
      char rssistr[10];
      sprintf(rssistr,"%ld",rssi);
      #ifdef MQTTFEATURE
      String mqttRootTopic = settingsManager.getAppSettings().mqttRootTopic;
      mqttClient.publish((String(mqttRootTopic) + "/rssiStatus").c_str(), rssistr);      
      #endif
    }  
}

#ifdef MQTTFEATURE
void connectMqttClient() {
  if (!mqttClient.connected() && mqttConfigValid) {
    #ifdef DEBUG
    Serial.print("(Re)connect to MQTT broker...");
    #endif
    // Attempt to connect
    bool connectResult;
    
    // connect with or witout authentication
    String lastWillTopic = settingsManager.getAppSettings().mqttRootTopic + "/lastLogMessage";
    String lastWillMessage = "FingerprintDoorbell disconnected unexpectedly";
    if (settingsManager.getAppSettings().mqttUsername.isEmpty() || settingsManager.getAppSettings().mqttPassword.isEmpty())
      connectResult = mqttClient.connect(settingsManager.getWifiSettings().hostname.c_str(),lastWillTopic.c_str(), 1, false, lastWillMessage.c_str());
    else
      connectResult = mqttClient.connect(settingsManager.getWifiSettings().hostname.c_str(), settingsManager.getAppSettings().mqttUsername.c_str(), settingsManager.getAppSettings().mqttPassword.c_str(), lastWillTopic.c_str(), 1, false, lastWillMessage.c_str());

    if (connectResult) {
      // success
      #ifdef DEBUG
      Serial.println("connected");
      #endif
      notifyClients("Connected to MQTT Server.");
      // Subscribe
      mqttClient.subscribe((settingsManager.getAppSettings().mqttRootTopic + "/ignoreTouchRing").c_str(), 1); // QoS = 1 (at least once)
      mqttClient.subscribe((settingsManager.getAppSettings().mqttRootTopic + "/LedTouchRing").c_str(), 1); // QoS = 1 (at least once)
      #ifdef CUSTOM_GPIOS
        mqttClient.subscribe((settingsManager.getAppSettings().mqttRootTopic + "/customOutput1").c_str(), 1); // QoS = 1 (at least once)
        mqttClient.subscribe((settingsManager.getAppSettings().mqttRootTopic + "/customOutput2").c_str(), 1); // QoS = 1 (at least once)
      #endif



    } else {
      if (mqttClient.state() == 4 || mqttClient.state() == 5) {
        mqttConfigValid = false;
        notifyClients("Failed to connect to MQTT Server: bad credentials or not authorized. Will not try again, please check your settings.");
      } else {
        notifyClients(String("Failed to connect to MQTT Server, rc=") + mqttClient.state() + ", try again in 5 seconds");
      }
    }
  }
}
#endif


void doScan()
{
  Match match = fingerManager.scanFingerprint();
  String mqttRootTopic = settingsManager.getAppSettings().mqttRootTopic;
  #ifdef KNXFEATURE
  String door1List = settingsManager.getKNXSettings().door1_list;
  String door2List = settingsManager.getKNXSettings().door2_list;
  #endif
  switch(match.scanResult)
  {
    case ScanResult::noFinger:
      // standard case, occurs every iteration when no finger touchs the sensor
      if (match.scanResult != lastMatch.scanResult) {
        #ifdef DEBUG
        Serial.println("no finger");
        #endif
        #ifdef MQTTFEATURE
        mqttClient.publish((String(mqttRootTopic) + "/ring").c_str(), "off");
        mqttClient.publish((String(mqttRootTopic) + "/matchId").c_str(), "-1");
        mqttClient.publish((String(mqttRootTopic) + "/matchName").c_str(), "");
        mqttClient.publish((String(mqttRootTopic) + "/matchConfidence").c_str(), "-1");
        #endif
      }      
      break; 

    case ScanResult::matchFound:
      notifyClients( String("Match Found: ") + match.matchId + " - " + match.matchName  + " with confidence of " + match.matchConfidence );      
      doorBell_block_trigger = true; // block Doorbell for n seconds 
      if (match.scanResult != lastMatch.scanResult) {
        if (checkPairingValid()) {
          #ifdef MQTTFEATURE
          mqttClient.publish((String(mqttRootTopic) + "/ring").c_str(), "off");
          mqttClient.publish((String(mqttRootTopic) + "/matchId").c_str(), String(match.matchId).c_str());
          mqttClient.publish((String(mqttRootTopic) + "/matchName").c_str(), match.matchName.c_str());
          mqttClient.publish((String(mqttRootTopic) + "/matchConfidence").c_str(), String(match.matchConfidence).c_str());
          #endif
          #ifdef KNXFEATURE
             if ((isNumberInList(door1List, ',',match.matchId))||(isNumberInList(door2List, ',',match.matchId))){
              alarm_disable_trigger = true;              
              notifyKNX( String("AD_L1|2/ID") + match.matchId);
              #ifdef DEBUG
               Serial.println("Finger in list 1 or 2! Disable Alarm!");
              #endif
             }           
             
             if (isNumberInList(door1List, ',',match.matchId)){
              if(alarm_system_armed == true){
                  door1_delayed_trigger = true; // give time to the alarm system 
                }else{
                  door1_trigger = true;
                }              
              notifyKNX( String("D1/ID") + match.matchId + "/C" + match.matchConfidence );
              #ifdef DEBUG
               Serial.println("Finger in list 1! Open the door 1!");
              #endif
          }else if (isNumberInList(door2List, ',',match.matchId)){
              if(alarm_system_armed == true){
                  door2_delayed_trigger = true; // give time to the alarm system 
                }else{
                  door2_trigger = true;
                }                  
              notifyKNX( String("D2/ID") + match.matchId + "/C" + match.matchConfidence );
               #ifdef DEBUG
                Serial.println("Finger in List2! Open the door2!");
               #endif
          }else{
               notifyKNX( String("xx/ID") + match.matchId + "/C" + match.matchConfidence );
               doorBell_trigger = true; // ring if finger not maped
               #ifdef DEBUG
                Serial.println("Finger not in List1 and List2! - ring!");
               #endif
          }
          #endif
          #ifdef MQTTFEATURE
          #ifdef DEBUG
          Serial.println("MQTT message sent: Open the door!");
          #endif
          #endif
        } else {
          #ifdef MQTTFEATURE
          notifyClients("Security issue! Match was not sent by MQTT because of invalid sensor pairing! This could potentially be an attack! If the sensor is new or has been replaced by you do a (re)pairing in settings page.");
          #endif
          #ifdef KNXFEATURE
          notifyKNX("Pairing inval");
          #endif
        }
      }
      currentMode = Mode::wait; //replaces delay(2000) i hate delays // wait some time before next scan to let the LED blink
      break;

    case ScanResult::noMatchFound:
      notifyClients(String("No Match Found (Code ") + match.returnCode + ")");
      if (match.scanResult != lastMatch.scanResult) {        
        doorBell_trigger = true;        
        
        #ifdef MQTTFEATURE        
           mqttClient.publish((String(mqttRootTopic) + "/matchId").c_str(), "-1");
           mqttClient.publish((String(mqttRootTopic) + "/matchName").c_str(), "");
           mqttClient.publish((String(mqttRootTopic) + "/matchConfidence").c_str(), "-1");
        #endif
        #ifdef DEBUG
           Serial.println("Message sent: ring the bell!");
        #endif                       
        currentMode = Mode::wait; //replaces delay(2000) i hate delays // wait some time before next scan to let the LED blink        
      } else {
        
      }
      break;

    case ScanResult::error:
      notifyClients(String("ScanResult Error (Code ") + match.returnCode + ")");
      break;
  };
  lastMatch = match;

}

void doEnroll()
{
  int id = enrollId.toInt();
  if (id < 1 || id > 200) {
    notifyClients("Invalid memory slot id '" + enrollId + "'");
    return;
  }

  NewFinger finger = fingerManager.enrollFinger(id, enrollName, settingsManager.getAppSettings().tpScans);
  if (finger.enrollResult == EnrollResult::ok) {
    notifyClients("Enrollment successfull. You can now use your new finger for scanning.");
    updateClientsFingerlist(fingerManager.getFingerListAsHtmlOptionList());
  }  else if (finger.enrollResult == EnrollResult::error) {
    notifyClients(String("Enrollment failed. (Code ") + finger.returnCode + ")");
  }
}

void reboot()
{
  notifyClients("System is rebooting now...");
  delay(1000);
    
  #ifdef MQTTFEATURE
  mqttClient.disconnect();
  #endif
  espClient.stop();
  dnsServer.stop();
  webServer.end();
  WiFi.disconnect();
  ESP.restart();
}

void setup()
{  
  #ifdef DEBUG
  // open serial monitor for debug infos  
  Serial.begin(115200);
  while (!Serial);  // For Yun/Leo/Micro/Zero/...
  delay(100);
  #endif  

  #ifdef CUSTOM_GPIOS     
    pinMode(customOutput1, OUTPUT); 
    pinMode(customOutput2, OUTPUT);
    pinMode(doorbellOutputPin, OUTPUT);
    #ifdef ESP32 
    pinMode(customInput1, INPUT_PULLDOWN);
    pinMode(customInput2, INPUT_PULLDOWN);
    #endif
    #ifdef ESP8266
    pinMode(customInput1);
    pinMode(customInput2);
    #endif
  #endif  

  settingsManager.loadWifiSettings();
  settingsManager.loadAppSettings();  
  settingsManager.loadKNXSettings(); 
  fingerManager.connect();
  
  if (!checkPairingValid())
    notifyClients("Security issue! Pairing with sensor is invalid. This could potentially be an attack! If the sensor is new or has been replaced by you do a (re)pairing in settings page. MQTT messages regarding matching fingerprints will not been sent until pairing is valid again.");

  if (fingerManager.isFingerOnSensor() || !settingsManager.isWifiConfigured())
  {
    // ring touched during startup or no wifi settings stored -> wifi config mode
    currentMode = Mode::wificonfig;
    #ifdef DEBUG
    Serial.println("Started WiFi-Config mode");
    #endif
    fingerManager.setLedRingWifiConfig();
    initWiFiAccessPointForConfiguration();
    startWebserver();

  } else {
    #ifdef DEBUG
    Serial.println("Started normal operating mode");
    #endif
    currentMode = Mode::scan;
    if (initWifi()) {
      startWebserver();
      #ifdef MQTTFEATURE
      if (settingsManager.getAppSettings().mqttServer.isEmpty()) {
        mqttConfigValid = false;
        notifyClients("Error: No MQTT Broker is configured! Please go to settings and enter your server URL + user credentials.");
      } else {
        delay(1000);
        IPAddress mqttServerIp;
        if (WiFi.hostByName(settingsManager.getAppSettings().mqttServer.c_str(), mqttServerIp))
        {
          mqttConfigValid = true;
          #ifdef DEBUG
          //Serial.println("IP used for MQTT server: " + mqttServerIp.toString());
          Serial.println("IP used for MQTT server: " + mqttServerIp.toString() + " | Port: " + String(settingsManager.getAppSettings().mqttPort));          
          #endif          
          //mqttClient.setServer(mqttServerIp , 1883);
          mqttClient.setServer(mqttServerIp , settingsManager.getAppSettings().mqttPort);
          mqttClient.setCallback(mqttCallback);
          connectMqttClient();
        }
        else {
          mqttConfigValid = false;
          notifyClients("MQTT Server '" + settingsManager.getAppSettings().mqttServer + "' not found. Please check your settings.");
        }
      }
      #endif
      if (fingerManager.connected)
        fingerManager.setLedRingReady();              
      else
        fingerManager.setLedRingError();
      
      #ifdef KNXFEATURE      
      SetupKNX();
      knx.start();
      if (loadRemanentIntFromPrefs("alarm_armed") > 0){
        alarm_system_armed = true;
      }else if (loadRemanentIntFromPrefs("alarm_armed") == 0){
        alarm_system_armed = false;
      }

      if (loadRemanentIntFromPrefs("led") > 0){
        fingerManager.setLedTouchRing(true);
      }else if(loadRemanentIntFromPrefs("led") == 0){
        fingerManager.setLedTouchRing(false);
      }   

      if (loadRemanentIntFromPrefs("touch_on") > 0){
        fingerManager.setIgnoreTouchRing(false);
      }else if (loadRemanentIntFromPrefs("touch_on") == 0){
        fingerManager.setIgnoreTouchRing(true);
      }      


      #endif

    }  else {
      fingerManager.setLedRingError();
      shouldReboot = true;
    }

  }
  
}

void loop()
{
  // shouldReboot flag for supporting reboot through webui
  if (shouldReboot) {
    reboot();
  }
  
  // Reconnect handling
  if (currentMode != Mode::wificonfig)
  {
    unsigned long now = millis();
    // reconnect WiFi if down for 30s
    if ((WiFi.status() != WL_CONNECTED) && (now - wifiReconnectPreviousMillis >= 5000ul)) {
      #ifdef DEBUG
      Serial.println("Reconnecting to WiFi...");
      #endif
      notifyClients("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      wifiReconnectPreviousMillis = now;
    }

    // reconnect mqtt if down
    #ifdef MQTTFEATURE
    if (!settingsManager.getAppSettings().mqttServer.isEmpty()) {
      if (!mqttClient.connected() && (now - mqttReconnectPreviousMillis >= 5000ul)) {
        connectMqttClient();
        mqttReconnectPreviousMillis = now;
      }
      mqttClient.loop();
    }
    #endif
  }



  // do the actual loop work
  switch (currentMode)
  {
  case Mode::scan:
    if (fingerManager.connected)
      doScan();
    break;

  case Mode::wait:
     doWait(wait_Duration);
    break;
  
  case Mode::enroll:
    doEnroll();
    currentMode = Mode::scan; // switch back to scan mode after enrollment is done
    break;
  
  case Mode::wificonfig:
    dnsServer.processNextRequest(); // used for captive portal redirect
    break;

  case Mode::maintenance:
    // do nothing, give webserver exclusive access to sensor (not thread-safe for concurrent calls)
    break;

  }

  // enter maintenance mode (no continous scanning) if requested
  if (needMaintenanceMode)
    currentMode = Mode::maintenance;

doRssiStatus();
 
#ifdef KNXFEATURE 
knx.loop();
doAlarmDisable();
doDoor1TriggerDelay();
doDoor2TriggerDelay();
doDoor1();
doDoor2();
ledStateToKNX();
touchStateToKNX();
#endif

doDoorbellBlock();
doDoorbell();

#ifdef CUSTOM_GPIOS
    // read custom inputs and publish by MQTT
    bool i1;
    bool i2;
    i1 = (digitalRead(customInput1) == HIGH);
    i2 = (digitalRead(customInput2) == HIGH);

    String mqttRootTopic = settingsManager.getAppSettings().mqttRootTopic;
    if (i1 != customInput1Value) {
        if (i1)
          mqttClient.publish((String(mqttRootTopic) + "/customInput1").c_str(), "on");      
        else
          mqttClient.publish((String(mqttRootTopic) + "/customInput1").c_str(), "off");      
    }

    if (i2 != customInput2Value) {
        if (i2)
          mqttClient.publish((String(mqttRootTopic) + "/customInput2").c_str(), "on");      
        else
          mqttClient.publish((String(mqttRootTopic) + "/customInput2").c_str(), "off");  
    }
    
    //doCustomOutputs();

    customInput1Value = i1;
    customInput2Value = i2;
#endif  

}

