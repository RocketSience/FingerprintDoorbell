#include "SettingsManager.h"
#include <Crypto.h>

bool SettingsManager::loadWifiSettings() {
    Preferences preferences;
    if (preferences.begin("wifiSettings", true)) {
        wifiSettings.ssid = preferences.getString("ssid", String(""));
        wifiSettings.password = preferences.getString("password", String(""));
        wifiSettings.hostname = preferences.getString("hostname", String("FingerprintDoorbell"));
        preferences.end();
        return true;
    } else {
        return false;
    }
}

bool SettingsManager::loadAppSettings() {
    Preferences preferences;
    if (preferences.begin("appSettings", true)) {
        appSettings.tpScans = preferences.getUShort("tpScans", (uint16_t) 5);
        appSettings.mqttServer = preferences.getString("mqttServer", String(""));
        appSettings.mqttPort = preferences.getUShort("mqttPort", (uint16_t) 1883);
        appSettings.mqttUsername = preferences.getString("mqttUsername", String(""));
        appSettings.mqttPassword = preferences.getString("mqttPassword", String(""));
        appSettings.mqttRootTopic = preferences.getString("mqttRootTopic", String("fingerprintDoorbell"));
        appSettings.ntpServer = preferences.getString("ntpServer", String("pool.ntp.org"));
        appSettings.sensorPin = preferences.getString("sensorPin", "00000000");
        appSettings.sensorPairingCode = preferences.getString("pairingCode", "");
        appSettings.sensorPairingValid = preferences.getBool("pairingValid", false);
        preferences.end();
        return true;
    } else {
        return false;
    }
}

bool SettingsManager::loadKNXSettings() {
    Preferences preferences;
    if (preferences.begin("knxSettings", true)) {
        knxSettings.door1_ga = preferences.getString("door1_ga", String(""));
        knxSettings.door2_ga = preferences.getString("door2_ga", String(""));
        knxSettings.doorbell_ga = preferences.getString("doorbell_ga", String(""));        
        knxSettings.alarmdisable_ga = preferences.getString("alarmdisable_ga", String("")); 
        knxSettings.alarmarmed_ga = preferences.getString("alarmarmed_ga", String("")); 
        knxSettings.autounarm_ga = preferences.getString("autounarm_ga", String("")); 
        knxSettings.led_ga = preferences.getString("led_ga", String(""));
        knxSettings.ledstate_ga = preferences.getString("ledstate_ga", String(""));        
        knxSettings.touch_ga = preferences.getString("touch_ga", String(""));
        knxSettings.touchstate_ga = preferences.getString("touchstate_ga", String(""));
        knxSettings.message_ga = preferences.getString("message_ga", String(""));                
        knxSettings.knx_pa = preferences.getString("knx_pa", String("1.1.1"));
        knxSettings.knxrouter_ip = preferences.getString("knxrouter_ip", String("192.168.0.199"));
        knxSettings.door1_list = preferences.getString("door1_list", String(""));        
        knxSettings.door2_list = preferences.getString("door2_list", String(""));       
        preferences.end();
        return true;
    } else {
        return false;
    }
}
   
void SettingsManager::saveWifiSettings() {
    Preferences preferences;
    preferences.begin("wifiSettings", false); 
    preferences.putString("ssid", wifiSettings.ssid);
    preferences.putString("password", wifiSettings.password);
    preferences.putString("hostname", wifiSettings.hostname);
    preferences.end();
}

void SettingsManager::saveKNXSettings() {
    Preferences preferences;
    preferences.begin("knxSettings", false); 
    preferences.putString("door1_ga", knxSettings.door1_ga);
    preferences.putString("door2_ga", knxSettings.door2_ga);
    preferences.putString("doorbell_ga", knxSettings.doorbell_ga);
    preferences.putString("alarmdisable_ga", knxSettings.alarmdisable_ga);
    preferences.putString("alarmarmed_ga", knxSettings.alarmarmed_ga);
    preferences.putString("autounarm_ga", knxSettings.autounarm_ga);   
    preferences.putString("led_ga", knxSettings.led_ga);
    preferences.putString("ledstate_ga", knxSettings.ledstate_ga);
    preferences.putString("touch_ga", knxSettings.touch_ga);
    preferences.putString("touchstate_ga", knxSettings.touchstate_ga);
    preferences.putString("message_ga", knxSettings.message_ga);
    preferences.putString("knx_pa", knxSettings.knx_pa);
    preferences.putString("knxrouter_ip", knxSettings.knxrouter_ip);
    preferences.putString("door1_list", knxSettings.door1_list);
    preferences.putString("door2_list", knxSettings.door2_list);
    preferences.end();
}

void SettingsManager::saveAppSettings() {
    Preferences preferences;
    preferences.begin("appSettings", false); 
    preferences.putUShort("tpScans", appSettings.tpScans);
    preferences.putString("mqttServer", appSettings.mqttServer);
    preferences.putUShort("mqttPort", appSettings.mqttPort);
    preferences.putString("mqttUsername", appSettings.mqttUsername);
    preferences.putString("mqttPassword", appSettings.mqttPassword);
    preferences.putString("mqttRootTopic", appSettings.mqttRootTopic);
    preferences.putString("ntpServer", appSettings.ntpServer);
    preferences.putString("sensorPin", appSettings.sensorPin);
    preferences.putString("pairingCode", appSettings.sensorPairingCode);
    preferences.putBool("pairingValid", appSettings.sensorPairingValid);
    preferences.end();
}

WifiSettings SettingsManager::getWifiSettings() {
    return wifiSettings;
}

KNXSettings SettingsManager::getKNXSettings() {
    return knxSettings;
}

void SettingsManager::saveWifiSettings(WifiSettings newSettings) {
    wifiSettings = newSettings;
    saveWifiSettings();
}

AppSettings SettingsManager::getAppSettings() {
    return appSettings;
}

void SettingsManager::saveAppSettings(AppSettings newSettings) {
    appSettings = newSettings;
    saveAppSettings();
}

void SettingsManager::saveKNXSettings(KNXSettings newSettings) {
    knxSettings = newSettings;
    saveKNXSettings();
}

bool SettingsManager::isWifiConfigured() {
    if (wifiSettings.ssid.isEmpty() || wifiSettings.password.isEmpty())
        return false;
    else
        return true;
}

bool SettingsManager::isKNXConfigured() {
    if (knxSettings.door1_ga.isEmpty() || knxSettings.knx_pa.isEmpty())
        return false;
    else
        return true;
}

bool SettingsManager::deleteAppSettings() {
    bool rc;
    Preferences preferences;
    rc = preferences.begin("appSettings", false); 
    if (rc)
        rc = preferences.clear();
    preferences.end();
    return rc;
}

bool SettingsManager::deleteWifiSettings() {
    bool rc;
    Preferences preferences;
    rc = preferences.begin("wifiSettings", false); 
    if (rc)
        rc = preferences.clear();
    preferences.end();
    return rc;
}

bool SettingsManager::deleteKNXSettings() {
    bool rc;
    Preferences preferences;
    rc = preferences.begin("knxSettings", false); 
    if (rc)
        rc = preferences.clear();
    preferences.end();
    return rc;
}

String SettingsManager::generateNewPairingCode() {

    /* Create a SHA256 hash */
    SHA256 hasher;

    /* Put some unique values as input in our new hash */
    //hasher.doUpdate( String(esp_random()).c_str() ); // random number
    hasher.doUpdate( String(rand()).c_str() ); // random number // esp_rand() does not exist for esp8266
    hasher.doUpdate( String(millis()).c_str() ); // time since boot
    hasher.doUpdate(getTimestampString().c_str()); // current time (if NTP is available)
    hasher.doUpdate(appSettings.mqttUsername.c_str());
    hasher.doUpdate(appSettings.mqttPassword.c_str());
    hasher.doUpdate(wifiSettings.ssid.c_str());
    hasher.doUpdate(wifiSettings.password.c_str());

    /* Compute the final hash */
    byte hash[SHA256_SIZE];
    hasher.doFinal(hash);
    
    // Convert our 32 byte hash to 32 chars long hex string. When converting the entire hash to hex we would need a length of 64 chars.
    // But because we only want a length of 32 we only use the first 16 bytes of the hash. I know this will increase possible collisions,
    // but for detecting a sensor replacement (which is the use-case here) it will still be enough.
    char hexString[33];
    hexString[32] = 0; // null terminatation byte for converting to string later
    for (byte i=0; i < 16; i++) // use only the first 16 bytes of hash
    {
        sprintf(&hexString[i*2], "%02x", hash[i]);
    }

    return String((char*)hexString);
}

