#include "Arduino.h"
#include "FingerprintManager.h"
#include "global.h"
#include <Adafruit_Fingerprint.h>
#define mySerial Serial2

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

bool FingerprintManager::connect() {
  
    // initialize input pins    
    pinMode(touchRingPin, INPUT_PULLDOWN);

    #ifdef DEBUG
    Serial.println("\n\nAdafruit finger detect test");
    #endif

    // set the data rate for the sensor serial port    
    finger.begin(57600);        
    delay(50);
    if (finger.verifyPassword()) {
        #ifdef DEBUG
        Serial.println("Found fingerprint sensor!");
        #endif
    } else {
        delay(5000); // wait a bit longer for sensor to start before 2nd try (usually after a OTA-Update the esp32 is faster with startup than the fingerprint sensor)        
        if (finger.verifyPassword()) { 
          #ifdef DEBUG
          Serial.println("Found fingerprint sensor!");
          #endif
        } else {
          #ifdef DEBUG
          Serial.println("Did not find fingerprint sensor :(");
          #endif
          connected = false;
          return connected;
        }
    }   
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_BLUE, 0); // sensor connected signal    

    #ifdef DEBUG
    Serial.println(F("Reading sensor parameters"));
    #endif
    finger.getParameters();
    #ifdef DEBUG
    Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
    Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
    Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
    Serial.print(F("Security level: ")); Serial.println(finger.security_level);
    Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
    Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
    Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);
    #endif

    finger.getTemplateCount();
    #ifdef DEBUG
    Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
    #endif

    loadFingerListFromPrefs();

    connected = true;
    return connected;

    //updateTouchState(false);
}

void FingerprintManager::updateTouchState(bool touched)
{
  if ((touched != lastTouchState) || (ignoreTouchRing != lastIgnoreTouchRing)) {
      // check if sensor or ring is touched
      if (touched) {
        // turn touch indicator on:
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 8, FINGERPRINT_LED_WHITE, 0);
      } else {
        // turn touch indicator off:
        setLedRingReady();
     }
  }
  lastTouchState = touched;
  lastIgnoreTouchRing = ignoreTouchRing;
  
}


Match FingerprintManager::scanFingerprint() {
  
  Match match;
  match.scanResult = ScanResult::error;

  if (!connected) {
      return match;
  }


  // finger detection by capacitive touchRing state (increased sensitivy but error prone due to rain)
  bool ringTouched = false;
  if (!ignoreTouchRing)
  {
    if (isRingTouched())
      ringTouched = true;
    if (ringTouched || lastTouchState) { 
        updateTouchState(true);
        //Serial.println("touched");
    } else {
        updateTouchState(false);
        match.scanResult = ScanResult::noFinger;
        return match;
    }

  }
  

  bool doAnotherScan = true;
  int scanPass = 0;
  while (doAnotherScan)
  {
    doAnotherScan = false;
    scanPass++;

    ///////////////////////////////////////////////////////////
    // STEP 1: Get Image from Sensor
    ///////////////////////////////////////////////////////////
    bool doImaging = true;
    int imagingPass = 0;
    while (doImaging)
    {
      doImaging = false;
      imagingPass++;
      //Serial.println(String("Get Image try ") + imagingPass);
      match.returnCode = finger.getImage();
      switch (match.returnCode) {
        case FINGERPRINT_OK:
          // Important: do net set touch state to true yet! Reason:
          // - if touchRing is NOT ignored, updateTouchState(true) was already called a few lines up, ring is already flashing red
          // - if touchRing IS ignored, wait for next step because image still can be "too messy" (=raindrop on sensor), and we don't want to flash red in this case
          //updateTouchState(true);
          //Serial.println("Image taken");
          break;
        case FINGERPRINT_NOFINGER:
        case FINGERPRINT_PACKETRECIEVEERR: // occurs from time to time, handle it like a "nofinger detected but touched" situation
          if (ringTouched) {
            // no finger on sensor but ring was touched -> ring event
            //Serial.println("ring touched");
            updateTouchState(true);
            if (imagingPass < 15) // up to x image passes in a row are taken after touch ring was touched until noFinger will raise a noMatchFound event
            {
              doImaging = true; // scan another image
              //delay(50);
              break;
            } else {
              //Serial.println("15 times no image after touching ring");
              match.scanResult = ScanResult::noMatchFound;
              return match;
            }
          } else  {
            if (ignoreTouchRing && scanPass > 1) {
              // the scan(s) in last iteration(s) have not found any match, now the finger was released (=no finger) -> return "no match" as result
              match.scanResult = ScanResult::noMatchFound;
            } else {
              match.scanResult = ScanResult::noFinger;
              updateTouchState(false);
            }
            return match;
          }
        case FINGERPRINT_IMAGEFAIL:
          #ifdef DEBUG
          Serial.println("Imaging error");
          #endif
          updateTouchState(true);
          return match;
        default:
          #ifdef DEBUG
          Serial.println("Unknown error");
          #endif
          return match;
      }
    
    }

    ///////////////////////////////////////////////////////////
    // STEP 2: Convert Image to feature map
    ///////////////////////////////////////////////////////////
    match.returnCode = finger.image2Tz();
    switch (match.returnCode) {
      case FINGERPRINT_OK:
        //Serial.println("Image converted");
        updateTouchState(true);
        break;
      case FINGERPRINT_IMAGEMESS:
        #ifdef DEBUG
        Serial.println("Image too messy");
        #endif
        return match;
      case FINGERPRINT_PACKETRECIEVEERR:
        #ifdef DEBUG
        Serial.println("Communication error");
        #endif
        return match;
      case FINGERPRINT_FEATUREFAIL:
        #ifdef DEBUG
        Serial.println("Could not find fingerprint features");
        #endif
        return match;
      case FINGERPRINT_INVALIDIMAGE:
        #ifdef DEBUG
        Serial.println("Could not find fingerprint features");
        #endif
        return match;
      default:
        #ifdef DEBUG
        Serial.println("Unknown error");
        #endif
        return match;
    }

    ///////////////////////////////////////////////////////////
    // STEP 3: Search DB for matching features
    ///////////////////////////////////////////////////////////
    match.returnCode = finger.fingerSearch();
    if (match.returnCode == FINGERPRINT_OK) {
        // found a match!
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_GREEN);
        
        match.scanResult = ScanResult::matchFound;
        match.matchId = finger.fingerID;
        match.matchConfidence = finger.confidence;
        match.matchName = fingerList[finger.fingerID];
      
    } else if (match.returnCode == FINGERPRINT_PACKETRECIEVEERR) {
        #ifdef DEBUG
        Serial.println("Communication error");
        #endif

    } else if (match.returnCode == FINGERPRINT_NOTFOUND) {
        #ifdef DEBUG
        Serial.println(String("Did not find a match. (Scan #") + scanPass + String(" of 5)"));
        #endif
        match.scanResult = ScanResult::noMatchFound;
        // turn touch indicator to error:
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 0);
        if (scanPass < 5) // max 5 Scans until no match found is given back as result
          doAnotherScan = true;

    } else {
        #ifdef DEBUG
        Serial.println("Unknown error");
        #endif
    }

  } //while

  return match;

}



// Preferences
void FingerprintManager::loadFingerListFromPrefs() {
  Preferences preferences;
  preferences.begin("fingerList", true); 
  int counter = 0;
  for (int i=1; i<=200; i++) {
    String key = String(i);
    if (preferences.isKey(key.c_str())) {
      fingerList[i] = preferences.getString(key.c_str(), String("@empty"));
      counter++;
    }
    else
      fingerList[i] = String("@empty");
  }
  #ifdef DEBUG
  Serial.println(String(counter) + " fingers loaded from preferences.");
  #endif
  if (counter != finger.templateCount)
    notifyClients(String("Warning: Fingerprint count mismatch! ") + finger.templateCount + " fingerprints stored on sensor, but we are aware of " + counter + " fingerprints.");
  preferences.end();
}


// Add/Enroll fingerprint
NewFinger FingerprintManager::enrollFinger(int id, String name, int samples = 5) {

  NewFinger newFinger;
  newFinger.enrollResult = EnrollResult::error;

  lastTouchState = true; // after enrollment, scan mode kicks in again. Force update of the ring light back to normal on first iteration of scan mode.

  
  notifyClients(String("Enrollment for id #") + id + " started. We need to scan your finger "+ samples +" times until enrollment is completed.");

  
  // Repeat n times to get better resulting templates (as stated in R503 documentation up to 6 combined image samples possible, but I got an communication error when trying more than 5 samples, so dont go >5)
  for (int nTimes=1; nTimes<=samples; nTimes++)
  {
      notifyClients(String("Take #" + String(nTimes))+ " (place your finger on the sensor until led ring stops flashing, then remove it).");

      if (nTimes != 1) // not on first run
      {
        //delay(2000);
        newFinger.returnCode = 0xFF;
        while (newFinger.returnCode != FINGERPRINT_NOFINGER) {
          newFinger.returnCode = finger.getImage();
        }
      }
      
      #ifdef DEBUG
      Serial.print("Taking image sample "); Serial.print(nTimes); Serial.print(": ");
      #endif
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_PURPLE, 0);
      newFinger.returnCode = 0xFF;
      while (newFinger.returnCode != FINGERPRINT_OK) {
        newFinger.returnCode = finger.getImage();
        switch (newFinger.returnCode) {
        case FINGERPRINT_OK:
          #ifdef DEBUG
          Serial.print("taken, ");
          #endif
          break;
        case FINGERPRINT_NOFINGER:
          break;
        case FINGERPRINT_PACKETRECIEVEERR:
          #ifdef DEBUG
          Serial.print("Communication error, ");
          #endif
          break;
        case FINGERPRINT_IMAGEFAIL:
          #ifdef DEBUG
          Serial.print("Imaging error, ");
          #endif
          break;
        default:
          #ifdef DEBUG
          Serial.print("Unknown error, ");
          #endif
          break;
        }
      }
    
      // OK success!
    
      newFinger.returnCode = finger.image2Tz(nTimes);
      switch (newFinger.returnCode) {
        case FINGERPRINT_OK:
          #ifdef DEBUG
          Serial.print("converted");
          #endif
          break;
        case FINGERPRINT_IMAGEMESS:
          #ifdef DEBUG
          Serial.print("too messy");
          #endif
          return newFinger;
        case FINGERPRINT_PACKETRECIEVEERR:
          #ifdef DEBUG
          Serial.print("Communication error");
          #endif
          return newFinger;
        case FINGERPRINT_FEATUREFAIL:
          #ifdef DEBUG
          Serial.print("Could not find fingerprint features");
          #endif
          return newFinger;
        case FINGERPRINT_INVALIDIMAGE:
          #ifdef DEBUG
          Serial.print("Could not find fingerprint features");
          #endif
          return newFinger;
        default:
          #ifdef DEBUG
          Serial.print("Unknown error");
          #endif
          return newFinger;
      }
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);

  }

  

  // OK converted!
  #ifdef DEBUG
  Serial.println();
  Serial.print("Creating model for #");  Serial.println(id);
  #endif

  newFinger.returnCode = finger.createModel();
  if (newFinger.returnCode == FINGERPRINT_OK) {
    #ifdef DEBUG
    Serial.println("Prints matched!");
    #endif
  } else if (newFinger.returnCode == FINGERPRINT_PACKETRECIEVEERR) {
    #ifdef DEBUG
    Serial.println("Communication error");
    #endif
    return newFinger;
  } else if (newFinger.returnCode == FINGERPRINT_ENROLLMISMATCH) {
    #ifdef DEBUG
    Serial.println("Fingerprints did not match");
    #endif
    return newFinger;
  } else {
    #ifdef DEBUG
    Serial.println("Unknown error");
    #endif
    return newFinger;
  }

  #ifdef DEBUG
  Serial.print("ID "); Serial.println(id);
  #endif
  newFinger.returnCode = finger.storeModel(id);
  if (newFinger.returnCode == FINGERPRINT_OK) {
    #ifdef DEBUG
    Serial.println("Stored!");
    #endif
    newFinger.enrollResult = EnrollResult::ok;
    // save to prefs
    fingerList[id] = name;
    Preferences preferences;
    preferences.begin("fingerList", false); 
    preferences.putString(String(id).c_str(), name);
    preferences.end();

  } else if (newFinger.returnCode == FINGERPRINT_PACKETRECIEVEERR) {
    #ifdef DEBUG
    Serial.println("Communication error");
    #endif
    return newFinger;
  } else if (newFinger.returnCode == FINGERPRINT_BADLOCATION) {
    #ifdef DEBUG
    Serial.println("Could not store in that location");
    #endif
    return newFinger;
  } else if (newFinger.returnCode == FINGERPRINT_FLASHERR) {
    #ifdef DEBUG
    Serial.println("Error writing to flash");
    #endif
    return newFinger;
  } else {
    #ifdef DEBUG
    Serial.println("Unknown error");
    #endif
    return newFinger;
  }

  //finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_RED);

  return newFinger;

}


void FingerprintManager::deleteFinger(int id) {
          
  if ((id > 0) && (id <= 200)) {
    int8_t result = finger.deleteModel(id);
    if (result != FINGERPRINT_OK) {
      notifyClients(String("Delete of finger template #") + id + " from sensor failed with code " + result);
      return;

    } else {
      fingerList[id] = "@empty";
      Preferences preferences;
      preferences.begin("fingerList", false); 
      preferences.remove (String(id).c_str());
      preferences.end();
      #ifdef DEBUG
      Serial.println(String("Finger template #") + id + " deleted from sensor and prefs.");
      #endif

    }
  }

}


void FingerprintManager::renameFinger(int id, String newName) {
  if ((id > 0) && (id <= 200)) {
    Preferences preferences;
    preferences.begin("fingerList", false); 
    preferences.putString(String(id).c_str(), newName);
    preferences.end();
    #ifdef DEBUG
    Serial.println(String("Finger template #") + id + " renamed from " + fingerList[id] + " to " + newName);
    #endif
    fingerList[id] = newName;
  }
}

String FingerprintManager::getFingerListAsHtmlOptionList() {
  String htmlOptions = "";
  int counter = 0;
  for (int i=1; i<=200; i++) {
    if (fingerList[i].compareTo("@empty") != 0) {
      String option;
      if (counter == 0)
        option = "<option value=\"" + String(i) + "\" selected>" + String(i) + " - " + fingerList[i] + "</option>";
      else 
        option = "<option value=\"" + String(i) + "\">" + String(i) + " - " + fingerList[i] + "</option>";
      htmlOptions += option;
      counter++;
    }
  }
  return htmlOptions;
}

void FingerprintManager::setLedTouchRing(bool state) {
  if (LedTouchRing != state) {
    LedTouchRing = state;
    if (state == true)
      notifyClients("LedTouchRing is now 'on'");
    else{
      notifyClients("LedTouchRing is now 'off'");
    }
      setLedRingReady();
  }
}

void FingerprintManager::setIgnoreTouchRing(bool state) {
  if (ignoreTouchRing != state) {
    ignoreTouchRing = state;
    if (state == true)
      notifyClients("IgnoreTouchRing is now 'on'");
    else{
      notifyClients("IgnoreTouchRing is now 'off'");
    }
    setLedRingReady();
  }
}


bool FingerprintManager::isRingTouched() {
  if (digitalRead(touchRingPin) == LOW) // LOW = touched. Caution: touchSignal on this pin occour only once (at beginning of touching the ring, not every iteration if you keep your finger on the ring)
      return true;
  else 
      return false;
}

bool FingerprintManager::isFingerOnSensor() {
  // get an image
  uint8_t returnCode = finger.getImage();
  if (returnCode == FINGERPRINT_OK) {
    // try to find fingerprint features in image, because image taken does not already means finger on sensor, could also be a raindrop
    returnCode = finger.image2Tz();
    if (returnCode == FINGERPRINT_OK)
      return true;
  }
  return false;
}
  
void FingerprintManager::setLedRingError() {
  finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_RED);
}

void FingerprintManager::setLedRingWifiConfig() {
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 250, FINGERPRINT_LED_RED);
}

void FingerprintManager::setLedRingReady() {
  if (LedTouchRing){
  if (!ignoreTouchRing)
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_CYAN);
    //finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 250, FINGERPRINT_LED_BLUE);	    
    else
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE); // Indicator switched off
  }
  else
    finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE); // just an indicator for me to see if touch ring is active or not
}	

bool FingerprintManager::deleteAll() {
  if (finger.emptyDatabase() == FINGERPRINT_OK)
  {
    bool rc;
    Preferences preferences;
    rc = preferences.begin("fingerList", false); 
    if (rc)
        rc = preferences.clear();
    preferences.end();

    for (int i=1; i<=200; i++) {
        fingerList[i] = String("@empty");
    };
    
    return rc;
  }
  else
    return false;

}



uint8_t FingerprintManager::writeNotepad(uint8_t pageNumber, const char *text, uint8_t length) {
  uint8_t data[34];
  
  if (length>32)
    length = 32;

  data[0] = FINGERPRINT_WRITENOTEPAD;
  data[1] = pageNumber;
  for (int i=0; i<length; i++)
    data[i+2] = text[i];

  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data), data);                                    
  finger.writeStructuredPacket(packet);
  if (finger.getStructuredPacket(&packet) != FINGERPRINT_OK)
    return FINGERPRINT_PACKETRECIEVEERR;
  if (packet.type != FINGERPRINT_ACKPACKET)
    return FINGERPRINT_PACKETRECIEVEERR;
  return packet.data[0];
}


uint8_t FingerprintManager::readNotepad(uint8_t pageNumber, char *text, uint8_t length) {
  uint8_t data[2];

  data[0] = FINGERPRINT_READNOTEPAD;
  data[1] = pageNumber;

  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data), data);                                    
  finger.writeStructuredPacket(packet);
  if (finger.getStructuredPacket(&packet) != FINGERPRINT_OK)
    return FINGERPRINT_PACKETRECIEVEERR;
  if (packet.type != FINGERPRINT_ACKPACKET)
    return FINGERPRINT_PACKETRECIEVEERR;
  
  if (packet.data[0] == FINGERPRINT_OK) {
    // read data payload
    for (uint8_t i=0; i<length; i++) {
      text[i] = packet.data[i+1];
    }
  }

  return packet.data[0];

}


String FingerprintManager::getPairingCode() {
  char buffer[33];
  buffer[32] = 0; // null termination needed for convertion to string at the end
  if (readNotepad(0, (char*)buffer, 32) == FINGERPRINT_OK)
    return String((char*)buffer);
  else
    return "";
}


bool FingerprintManager::setPairingCode(String pairingCode) {
  if (writeNotepad(0, pairingCode.c_str(), 32) == FINGERPRINT_OK)
    return true;
  else
    return false;
}


// ToDo: support sensor replacement by enable transferring of sensor DB to another sensor
void FingerprintManager::exportSensorDB() {

}
    
void FingerprintManager::importSensorDB() {

}

