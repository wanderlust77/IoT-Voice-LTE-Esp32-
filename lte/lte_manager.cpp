/*
 * lte_manager.cpp
 * 
 * Implementation of LTE modem manager
 */

#include "lte_manager.h"
#include "../utils/logger.h"
#include "../config.h"

// ============================================
// INITIALIZE LTE MANAGER
// ============================================
bool LTEManager::init(uint8_t txPin, uint8_t rxPin, uint8_t pwrkeyPin, uint8_t resetPin, uint32_t baudRate) {
  pinPwrkey = pwrkeyPin;
  pinReset = resetPin;
  initialized = false;
  powered = false;
  
  LOG_I("LTE", "Initializing LTE modem...");
  
  // Configure control pins
  pinMode(pinPwrkey, OUTPUT);
  pinMode(pinReset, OUTPUT);
  digitalWrite(pinPwrkey, HIGH);  // PWRKEY is active LOW
  digitalWrite(pinReset, HIGH);   // RESET is active LOW
  
  // Initialize UART (Serial2 on ESP32)
  modemSerial = &Serial2;
  modemSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  
  // Clear any pending data
  clearSerialBuffer();
  
  initialized = true;
  LOG_I("LTE", "LTE manager initialized");
  
  return true;
}

// ============================================
// POWER ON MODEM
// ============================================
bool LTEManager::powerOn() {
  if (!initialized) {
    LOG_E("LTE", "Not initialized");
    return false;
  }
  
  LOG_I("LTE", "Powering on modem...");
  
  // Pulse PWRKEY low for 1.5 seconds
  digitalWrite(pinPwrkey, LOW);
  delay(1500);
  digitalWrite(pinPwrkey, HIGH);
  
  // Wait for modem to boot (5 seconds)
  LOG_I("LTE", "Waiting for modem boot...");
  delay(5000);
  
  // Test communication
  clearSerialBuffer();
  for (int i = 0; i < 3; i++) {
    if (sendATCommand("AT", "OK", 2000)) {
      powered = true;
      LOG_I("LTE", "Modem powered on successfully");
      return true;
    }
    delay(1000);
  }
  
  LOG_E("LTE", "Failed to communicate with modem");
  return false;
}

// ============================================
// POWER OFF MODEM
// ============================================
bool LTEManager::powerOff() {
  if (!powered) {
    return true;
  }
  
  LOG_I("LTE", "Powering off modem...");
  
  // Pulse PWRKEY to turn off
  digitalWrite(pinPwrkey, LOW);
  delay(1500);
  digitalWrite(pinPwrkey, HIGH);
  
  powered = false;
  return true;
}

// ============================================
// CHECK NETWORK REGISTRATION
// ============================================
bool LTEManager::checkNetwork(uint32_t timeout_ms) {
  if (!powered) {
    LOG_E("LTE", "Modem not powered");
    return false;
  }
  
  LOG_I("LTE", "Checking network registration...");
  
  // Check SIM status
  if (!sendATCommand("AT+CPIN?", "+CPIN: READY", 5000)) {
    LOG_E("LTE", "SIM not ready");
    return false;
  }
  
  // Wait for network registration
  unsigned long startTime = millis();
  while (millis() - startTime < timeout_ms) {
    String response;
    if (sendATCommandGetResponse("AT+CREG?", response, 5000)) {
      // Look for +CREG: 0,1 (registered) or +CREG: 0,5 (roaming)
      if (response.indexOf("+CREG: 0,1") >= 0 || response.indexOf("+CREG: 0,5") >= 0) {
        LOG_I("LTE", "Network registered");
        return true;
      }
      
      if (response.indexOf("+CREG: 0,3") >= 0) {
        LOG_E("LTE", "Network registration denied");
        return false;
      }
    }
    
    delay(2000);  // Check every 2 seconds
  }
  
  LOG_E("LTE", "Network registration timeout");
  return false;
}

// ============================================
// CONFIGURE BEARER APN
// ============================================
bool LTEManager::configureBearerAPN(const char* apn) {
  LOG_I("LTE", "Configuring bearer APN...");
  
  // Set connection type to GPRS
  if (!sendATCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", 5000)) {
    LOG_E("LTE", "Failed to set connection type");
    return false;
  }
  
  // Set APN
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
  if (!sendATCommand(cmd, "OK", 5000)) {
    LOG_E("LTE", "Failed to set APN");
    return false;
  }
  
  LOG_I("LTE", "Bearer APN configured");
  return true;
}

// ============================================
// OPEN BEARER CONNECTION
// ============================================
bool LTEManager::openBearer() {
  LOG_I("LTE", "Opening bearer...");
  
  // Check if already open
  String response;
  if (sendATCommandGetResponse("AT+SAPBR=2,1", response, 5000)) {
    if (response.indexOf("0.0.0.0") < 0) {
      LOG_I("LTE", "Bearer already open");
      return true;
    }
  }
  
  // Open bearer
  if (!sendATCommand("AT+SAPBR=1,1", "OK", 30000)) {
    LOG_E("LTE", "Failed to open bearer");
    return false;
  }
  
  // Verify bearer is open
  if (sendATCommandGetResponse("AT+SAPBR=2,1", response, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "Bearer status: %s", response.c_str());
  }
  
  LOG_I("LTE", "Bearer opened");
  return true;
}

// ============================================
// CLOSE BEARER CONNECTION
// ============================================
bool LTEManager::closeBearer() {
  LOG_I("LTE", "Closing bearer...");
  sendATCommand("AT+SAPBR=0,1", "OK", 10000);
  return true;
}

// ============================================
// HTTP GET REQUEST
// ============================================
bool LTEManager::httpGet(const char* url, uint8_t* buffer, size_t* length, size_t maxLength) {
  LOG_I("LTE", "HTTP GET...");
  
  *length = 0;
  
  // Initialize HTTP
  if (!httpInit()) {
    return false;
  }
  
  // Set URL
  if (!httpSetParameter("URL", url)) {
    httpTerminate();
    return false;
  }
  
  // Set CID
  if (!httpSetParameter("CID", "1")) {
    httpTerminate();
    return false;
  }
  
  // Execute GET
  int statusCode, dataLength;
  if (!httpAction(HTTP_GET, &statusCode, &dataLength)) {
    httpTerminate();
    return false;
  }
  
  Logger::printf(LOG_INFO, "LTE", "HTTP status: %d, length: %d", statusCode, dataLength);
  
  // Check status code
  if (statusCode != 200) {
    LOG_E("LTE", "HTTP request failed");
    httpTerminate();
    return false;
  }
  
  // Read data
  if (!httpRead(buffer, length, maxLength)) {
    httpTerminate();
    return false;
  }
  
  // Terminate HTTP
  httpTerminate();
  
  Logger::printf(LOG_INFO, "LTE", "HTTP GET complete: %d bytes", *length);
  return true;
}

// ============================================
// HTTP POST REQUEST
// ============================================
bool LTEManager::httpPost(const char* url, const uint8_t* data, size_t length) {
  LOG_I("LTE", "HTTP POST...");
  
  // Initialize HTTP
  if (!httpInit()) {
    return false;
  }
  
  // Set URL
  if (!httpSetParameter("URL", url)) {
    httpTerminate();
    return false;
  }
  
  // Set CID
  if (!httpSetParameter("CID", "1")) {
    httpTerminate();
    return false;
  }
  
  // Set content type
  if (!httpSetParameter("CONTENT", "application/octet-stream")) {
    httpTerminate();
    return false;
  }
  
  // Upload data
  if (!httpPostData(data, length)) {
    httpTerminate();
    return false;
  }
  
  // Execute POST
  int statusCode, dataLength;
  if (!httpAction(HTTP_POST, &statusCode, &dataLength)) {
    httpTerminate();
    return false;
  }
  
  Logger::printf(LOG_INFO, "LTE", "HTTP POST status: %d", statusCode);
  
  // Terminate HTTP
  httpTerminate();
  
  if (statusCode == 200 || statusCode == 201) {
    LOG_I("LTE", "HTTP POST complete");
    return true;
  }
  
  LOG_E("LTE", "HTTP POST failed");
  return false;
}

// ============================================
// UPDATE (process incoming data)
// ============================================
void LTEManager::update() {
  // Process any unsolicited messages from modem
  while (modemSerial->available()) {
    char c = modemSerial->read();
    // Could log unsolicited responses here if needed
  }
}

// ============================================
// SEND AT COMMAND
// ============================================
bool LTEManager::sendATCommand(const char* cmd, const char* expected, uint32_t timeout_ms) {
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  clearSerialBuffer();
  modemSerial->println(cmd);
  
  return waitForResponse(expected, timeout_ms);
}

// ============================================
// SEND AT COMMAND AND GET RESPONSE
// ============================================
bool LTEManager::sendATCommandGetResponse(const char* cmd, String& response, uint32_t timeout_ms) {
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  clearSerialBuffer();
  modemSerial->println(cmd);
  
  response = readSerial(timeout_ms);
  Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
  
  return response.length() > 0;
}

// ============================================
// WAIT FOR RESPONSE
// ============================================
bool LTEManager::waitForResponse(const char* expected, uint32_t timeout_ms) {
  String response = readSerial(timeout_ms);
  Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
  
  return response.indexOf(expected) >= 0;
}

// ============================================
// CLEAR SERIAL BUFFER
// ============================================
void LTEManager::clearSerialBuffer() {
  while (modemSerial->available()) {
    modemSerial->read();
  }
}

// ============================================
// READ SERIAL DATA
// ============================================
String LTEManager::readSerial(uint32_t timeout_ms) {
  String result = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout_ms) {
    while (modemSerial->available()) {
      char c = modemSerial->read();
      result += c;
      startTime = millis();  // Reset timeout on data received
    }
    delay(10);
  }
  
  return result;
}

// ============================================
// HTTP INIT
// ============================================
bool LTEManager::httpInit() {
  return sendATCommand("AT+HTTPINIT", "OK", 5000);
}

// ============================================
// HTTP SET PARAMETER
// ============================================
bool LTEManager::httpSetParameter(const char* param, const char* value) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"%s\",\"%s\"", param, value);
  return sendATCommand(cmd, "OK", 5000);
}

// ============================================
// HTTP ACTION
// ============================================
bool LTEManager::httpAction(HttpMethod method, int* statusCode, int* dataLength) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=%d", method);
  
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  // Wait for +HTTPACTION response (can take several seconds)
  String response = readSerial(30000);
  Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
  
  // Parse response: +HTTPACTION: <method>,<status>,<length>
  int actionStart = response.indexOf("+HTTPACTION:");
  if (actionStart >= 0) {
    int commaPos1 = response.indexOf(',', actionStart);
    int commaPos2 = response.indexOf(',', commaPos1 + 1);
    
    if (commaPos1 > 0 && commaPos2 > 0) {
      *statusCode = response.substring(commaPos1 + 1, commaPos2).toInt();
      *dataLength = response.substring(commaPos2 + 1).toInt();
      return true;
    }
  }
  
  return false;
}

// ============================================
// HTTP READ
// ============================================
bool LTEManager::httpRead(uint8_t* buffer, size_t* length, size_t maxLength) {
  modemSerial->println("AT+HTTPREAD");
  LOG_D("LTE", "TX: AT+HTTPREAD");
  
  // Wait for +HTTPREAD: header
  String response = readSerial(10000);
  Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
  
  // Parse +HTTPREAD: <length>
  int readStart = response.indexOf("+HTTPREAD:");
  if (readStart < 0) {
    LOG_E("LTE", "HTTPREAD response not found");
    return false;
  }
  
  int dataStart = response.indexOf('\n', readStart);
  if (dataStart < 0) {
    LOG_E("LTE", "Data start not found");
    return false;
  }
  dataStart++;  // Skip newline
  
  // Copy data to buffer
  size_t dataLen = response.length() - dataStart;
  size_t copyLen = (dataLen < maxLength) ? dataLen : maxLength;
  
  for (size_t i = 0; i < copyLen; i++) {
    buffer[i] = response[dataStart + i];
  }
  
  *length = copyLen;
  return true;
}

// ============================================
// HTTP POST DATA
// ============================================
bool LTEManager::httpPostData(const uint8_t* data, size_t length) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,10000", length);
  
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  // Wait for DOWNLOAD prompt
  if (!waitForResponse("DOWNLOAD", 5000)) {
    LOG_E("LTE", "DOWNLOAD prompt not received");
    return false;
  }
  
  // Send binary data
  modemSerial->write(data, length);
  LOG_D("LTE", "Sent binary data");
  
  // Wait for OK
  return waitForResponse("OK", 15000);
}

// ============================================
// HTTP TERMINATE
// ============================================
bool LTEManager::httpTerminate() {
  return sendATCommand("AT+HTTPTERM", "OK", 5000);
}
