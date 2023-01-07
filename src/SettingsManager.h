#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <Preferences.h>
#include "global.h"

struct WifiSettings {    
    String ssid = "";
    String password = "";
    String hostname = "";
};

struct KNXSettings {    
    String door1_ga = "1/3/90";
    String door2_ga = "1/3/90";
    String doorbell_ga = "1/3/90";
    String alarmdisable_ga = "1/3/90";    
    String alarmarmed_ga = "1/3/90";
    String autounarm_ga = "1/3/90";
    
    String message_ga = "1/3/90";
    String led_ga = "";
    String ledstate_ga = "";
    String touch_ga = "";
    String touchstate_ga = "";
    String door1_list = "";
    String door2_list = "";
    String knx_pa = "1.1.1";    
    String knxrouter_ip = "192.168.0.199";
};

struct AppSettings {
    uint16_t tpScans = 5;
    String mqttServer = "";
    String mqttUsername = "";
    String mqttPassword = "";
    uint16_t mqttPort = 11883;
    String mqttRootTopic = "fingerprintDoorbell";
    String ntpServer = "pool.ntp.org";    
    String sensorPin = "00000000";
    String sensorPairingCode = "";
    bool   sensorPairingValid = false;    
};

class SettingsManager {       
  private:
    WifiSettings wifiSettings;
    AppSettings appSettings;
    KNXSettings knxSettings;

    void saveWifiSettings();
    void saveAppSettings();
    void saveKNXSettings();

  public:
    bool loadWifiSettings();
    bool loadAppSettings();
    bool loadKNXSettings();

    WifiSettings getWifiSettings();
    void saveWifiSettings(WifiSettings newSettings);
    
    AppSettings getAppSettings();
    void saveAppSettings(AppSettings newSettings);

    KNXSettings getKNXSettings();
    void saveKNXSettings(KNXSettings newSettings);

    bool isWifiConfigured();
    bool isKNXConfigured();

    bool deleteAppSettings();
    bool deleteWifiSettings();
    bool deleteKNXSettings();

    String generateNewPairingCode();

};

#endif