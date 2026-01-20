/*
 * nfc_manager.h
 * 
 * NFC reader interface using PN532 in I2C mode
 */

#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

// ============================================
// NFC MANAGER CLASS
// ============================================
class NFCManager {
public:
  // Initialize NFC reader
  bool init(uint8_t sdaPin, uint8_t sclPin, uint8_t irqPin, uint8_t rstPin);
  
  // Read NFC UID (non-blocking with timeout)
  // Returns true if UID was read successfully
  // uid: buffer to store UID (min 10 bytes)
  // length: pointer to store UID length (4, 7, or 10 bytes)
  // timeout_ms: timeout in milliseconds (0 = non-blocking check)
  bool readUID(uint8_t* uid, uint8_t* length, uint32_t timeout_ms);
  
  // Check if card is present (quick check)
  bool isCardPresent();
  
  // Get firmware version (for testing)
  uint32_t getFirmwareVersion();

private:
  Adafruit_PN532 nfc;
  bool initialized;
  
  // Helper to format UID as hex string
  void formatUID(const uint8_t* uid, uint8_t length, char* buffer, size_t bufferSize);
};

#endif // NFC_MANAGER_H
