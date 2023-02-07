#ifndef FINGERPRINTMANAGER_H
#define FINGERPRINTMANAGER_H

#include <Adafruit_Fingerprint.h>
#include <Preferences.h>
#include "global.h"

#define FINGERPRINT_WRITENOTEPAD 0x18 // Write Notepad on sensor
#define FINGERPRINT_READNOTEPAD 0x19 // Read Notepad from sensor

#ifndef FINGERPRINT_LED_GREEN
#define FINGERPRINT_LED_GREEN 0x04       //!< Green LED
#endif

#ifndef FINGERPRINT_LED_YELLOW
#define FINGERPRINT_LED_YELLOW 0x05      //!< Yellow LED
#endif

#ifndef FINGERPRINT_LED_CYAN
#define FINGERPRINT_LED_CYAN 0x06        //!< Cyan LED
#endif

#ifndef FINGERPRINT_LED_WHITE
#define FINGERPRINT_LED_WHITE 0x07       //!< White LED
#endif


/*
  By using the touch ring as an additional input to the image sensor the sensitivity is much higher for door bell ring events. Unfortunately
  we cannot differ between touches on the ring by fingers or rain drops, so rain on the ring will cause false alarms.
*/
const int touchRingPin = 5;     // touch/wakeup pin connected to fingerprint sensor

enum class ScanResult { noFinger, matchFound, noMatchFound, error };
enum class EnrollResult { ok, error };

struct Match {
  ScanResult scanResult = ScanResult::noFinger;
  uint16_t matchId = 0;
  String matchName = "unknown";
  uint16_t matchConfidence = 0;
  uint8_t returnCode = 0;
};

struct NewFinger {
  EnrollResult enrollResult = EnrollResult::error;
  uint8_t returnCode = 0;
};

class FingerprintManager {       
  private:    
    bool lastTouchState = false;
    //String fingerList[201];
    int fingerCountOnSensor = 0;
    //bool ignoreTouchRing = false; // set to true when the sensor is usually exposed to rain to avoid false ring events. Can also be set conditional by a rain sensor over MQTT
    bool lastIgnoreTouchRing = false;
    //bool LedTouchRing = true;
    
    void updateTouchState(bool touched);
    bool isRingTouched();
    void loadFingerListFromPrefs();
    void disconnect();
    uint8_t writeNotepad(uint8_t pageNumber, const char *text, uint8_t length);
    uint8_t readNotepad(uint8_t pageNumber, char *text, uint8_t length);
    


  public:
    String fingerList[201];
    bool LedTouchRing = true;
    bool ignoreTouchRing = false; // set to true when the sensor is usually exposed to rain to avoid false ring events. Can also be set conditional by a rain sensor over MQTT
    bool connected;
    bool connect();
    Match scanFingerprint();
    NewFinger enrollFinger(int id, String name, int samples);
    void deleteFinger(int id);
    void renameFinger(int id, String newName);
    String getFingerListAsHtmlOptionList();
    void setIgnoreTouchRing(bool state);
    void setLedTouchRing(bool state);
    bool isFingerOnSensor();
    void setLedRingError();
    void setLedRingWifiConfig();
    void setLedRingReady();
    String getPairingCode();
    bool setPairingCode(String pairingCode);
    
    bool deleteAll();

    
    // functions for sensor replacement
    void exportSensorDB();
    void importSensorDB();

};

#endif