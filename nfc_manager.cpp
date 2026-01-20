/*
 * nfc_manager.cpp
 * 
 * Implementation of NFC reader interface
 */

#include "nfc_manager.h"
#include "logger.h"

// ============================================
// CONSTRUCTOR
// ============================================
NFCManager::NFCManager() {
  nfc = nullptr;
  initialized = false;
  pinIrq = 0;
  pinRst = 0;
}

// ============================================
// DESTRUCTOR
// ============================================
NFCManager::~NFCManager() {
  if (nfc != nullptr) {
    delete nfc;
    nfc = nullptr;
  }
}

// ============================================
// INITIALIZE NFC READER
// ============================================
bool NFCManager::init(uint8_t sdaPin, uint8_t sclPin, uint8_t irqPin, uint8_t rstPin) {
  initialized = false;
  pinIrq = irqPin;
  pinRst = rstPin;
  
  LOG_I("NFC", "Initializing PN532...");
  
  // Initialize I2C
  Wire.begin(sdaPin, sclPin);
  
  // Initialize PN532 (using I2C constructor)
  nfc = new Adafruit_PN532(irqPin, rstPin);
  if (nfc == nullptr) {
    LOG_E("NFC", "Failed to allocate PN532 object");
    return false;
  }
  
  nfc->begin();
  
  // Check for PN532 board
  uint32_t versiondata = nfc->getFirmwareVersion();
  if (!versiondata) {
    LOG_E("NFC", "PN532 not found! Check wiring.");
    delete nfc;
    nfc = nullptr;
    return false;
  }
  
  // Log firmware version
  Logger::printf(LOG_INFO, "NFC", "Found PN532 chip, firmware v%d.%d", 
                 (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
  
  // Configure board to read RFID tags
  nfc->SAMConfig();
  
  initialized = true;
  LOG_I("NFC", "PN532 initialized successfully");
  
  return true;
}

// ============================================
// READ NFC UID
// ============================================
bool NFCManager::readUID(uint8_t* uid, uint8_t* length, uint32_t timeout_ms) {
  if (!initialized) {
    LOG_E("NFC", "Not initialized");
    return false;
  }
  
  uint8_t uidLength;
  bool success;
  
  if (timeout_ms == 0) {
    // Non-blocking: just try once
    success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 0);
  } else {
    // Blocking with timeout
    success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeout_ms);
  }
  
  if (success) {
    *length = uidLength;
    
    // Log UID in hex format
    char uidStr[32];
    formatUID(uid, uidLength, uidStr, sizeof(uidStr));
    Logger::printf(LOG_INFO, "NFC", "UID read: %s (%d bytes)", uidStr, uidLength);
    
    return true;
  }
  
  return false;
}

// ============================================
// CHECK IF CARD IS PRESENT
// ============================================
bool NFCManager::isCardPresent() {
  if (!initialized || nfc == nullptr) {
    return false;
  }
  
  uint8_t uid[10];
  uint8_t uidLength;
  
  // Quick non-blocking check
  return nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 0);
}

// ============================================
// GET FIRMWARE VERSION
// ============================================
uint32_t NFCManager::getFirmwareVersion() {
  if (!initialized || nfc == nullptr) {
    return 0;
  }
  return nfc->getFirmwareVersion();
}

// ============================================
// FORMAT UID AS HEX STRING
// ============================================
void NFCManager::formatUID(const uint8_t* uid, uint8_t length, char* buffer, size_t bufferSize) {
  size_t pos = 0;
  for (uint8_t i = 0; i < length && pos < bufferSize - 3; i++) {
    snprintf(buffer + pos, bufferSize - pos, "%02X", uid[i]);
    pos += 2;
  }
  buffer[pos] = '\0';
}
