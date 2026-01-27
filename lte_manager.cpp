/*
 * lte_manager.cpp
 * 
 * Implementation of LTE modem manager
 */

#include "lte_manager.h"
#include "logger.h"
#include "config.h"

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
  
  // Log UART configuration
  Logger::printf(LOG_INFO, "LTE", "UART: RX=GPIO%d, TX=GPIO%d, Baud=%d", rxPin, txPin, baudRate);
  
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
  
  LOG_I("LTE", "Checking if modem is already on...");
  
  // First, check if modem is already powered on
  // Clear any pending unsolicited messages
  clearSerialBuffer();
  delay(500);
  
  // Try AT command with longer timeout to handle unsolicited messages
  for (int i = 0; i < 3; i++) {
    clearSerialBuffer();
    modemSerial->println("AT");
    Logger::printf(LOG_DEBUG, "LTE", "TX: AT");
    
    // Wait longer (3 seconds) to allow unsolicited messages + OK
    String response = readSerial(3000);
    Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
    
    if (response.indexOf("OK") >= 0) {
      powered = true;
      LOG_I("LTE", "Modem already powered on");
      return true;
    }
    delay(500);
  }
  
  // Modem not responding - power it on
  LOG_I("LTE", "Modem off, powering on...");
  
  // Pulse PWRKEY low for 1.5 seconds
  digitalWrite(pinPwrkey, LOW);
  delay(1500);
  digitalWrite(pinPwrkey, HIGH);
  
  // Wait for modem to boot (LTE modems can take 10-15 seconds)
  LOG_I("LTE", "Waiting for modem boot (up to 15s)...");
  
  // Try AT command every 2 seconds for up to 30 seconds
  // LTE modems can take longer to boot, especially after power-on
  for (int attempt = 0; attempt < 15; attempt++) {
    delay(2000);
    Logger::printf(LOG_INFO, "LTE", "Boot check %d/15...", attempt + 1);
    
    clearSerialBuffer();
    modemSerial->println("AT");
    Logger::printf(LOG_DEBUG, "LTE", "TX: AT");
    
    // Wait 3 seconds to allow unsolicited messages + OK
    String response = readSerial(3000);
    Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
    
    if (response.indexOf("OK") >= 0) {
      powered = true;
      Logger::printf(LOG_INFO, "LTE", "Modem responded after %d seconds", (attempt + 1) * 2);
      return true;
    }
  }
  
  LOG_E("LTE", "Failed to communicate with modem after power-on");
  LOG_I("LTE", "Check: 1) UART wiring, 2) Modem power (5V), 3) TX/RX not swapped");
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
  String pinResponse;
  if (!sendATCommandGetResponse("AT+CPIN?", pinResponse, 5000)) {
    LOG_E("LTE", "Failed to query SIM PIN status");
    return false;
  }
  
  // Check if SIM requires PIN
  if (pinResponse.indexOf("+CPIN: SIM PIN") >= 0) {
    LOG_I("LTE", "SIM requires PIN unlock");
    
    // Check if PIN is configured
    if (strlen(LTE_PIN) > 0) {
      char pinCmd[32];
      snprintf(pinCmd, sizeof(pinCmd), "AT+CPIN=%s", LTE_PIN);
      
      if (!sendATCommand(pinCmd, "OK", 5000)) {
        LOG_E("LTE", "Failed to unlock SIM with PIN");
        return false;
      }
      
      LOG_I("LTE", "SIM unlocked successfully");
      delay(2000);  // Wait for SIM to initialize after unlock
    } else {
      LOG_E("LTE", "SIM requires PIN but LTE_PIN not configured");
      return false;
    }
  } else if (pinResponse.indexOf("+CPIN: READY") >= 0) {
    LOG_I("LTE", "SIM ready (no PIN required)");
  } else {
    Logger::printf(LOG_ERROR, "LTE", "Unexpected SIM status: %s", pinResponse.c_str());
    return false;
  }
  
  // Enable network registration unsolicited result codes
  // This allows modem to automatically report registration status changes
  if (!sendATCommand("AT+CREG=2", "OK", 5000)) {
    LOG_W("LTE", "Failed to enable CREG unsolicited codes (continuing anyway)");
  } else {
    LOG_I("LTE", "Enabled network registration notifications");
  }
  
  // Check signal strength first (CSQ)
  String csqResponse;
  if (sendATCommandGetResponse("AT+CSQ", csqResponse, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "Signal strength: %s", csqResponse.c_str());
    // Parse CSQ: +CSQ: <rssi>,<ber>
    // rssi: 0-31 (higher is better), 99=unknown
    int csqPos = csqResponse.indexOf("+CSQ:");
    if (csqPos >= 0) {
      int commaPos = csqResponse.indexOf(',', csqPos);
      if (commaPos > 0) {
        int rssi = csqResponse.substring(csqPos + 6, commaPos).toInt();
        if (rssi == 99) {
          LOG_W("LTE", "Signal strength: Unknown (no signal?)");
        } else {
          Logger::printf(LOG_INFO, "LTE", "Signal strength: %d/31 (higher is better)", rssi);
          if (rssi < 10) {
            LOG_W("LTE", "Weak signal - may affect registration");
          }
        }
      }
    }
  }
  
  // Wait for network registration
  unsigned long startTime = millis();
  int checkCount = 0;
  int consecutiveFailures = 0;
  
  while (millis() - startTime < timeout_ms) {
    checkCount++;
    unsigned long elapsed = millis() - startTime;
    
    // If we've had multiple consecutive failures, try a simple AT command first
    // to verify modem is still responsive
    if (consecutiveFailures >= 3) {
      Logger::printf(LOG_WARN, "LTE", "Multiple failures detected, checking modem responsiveness...");
      clearSerialBuffer();
      if (sendATCommand("AT", "OK", 3000)) {
        LOG_I("LTE", "Modem is responsive, continuing registration check");
        consecutiveFailures = 0;
      } else {
        LOG_W("LTE", "Modem not responding - may be busy searching");
        delay(5000);  // Wait longer before retry
        consecutiveFailures++;
        continue;
      }
    }
    
    String response;
    clearSerialBuffer();  // Clear any pending data before query
    
    if (sendATCommandGetResponse("AT+CREG?", response, 5000)) {
      consecutiveFailures = 0;  // Reset failure counter on success
      
      // Log the full response for debugging
      Logger::printf(LOG_INFO, "LTE", "CREG check #%d (elapsed: %lu ms): %s", 
                     checkCount, elapsed, response.c_str());
      
      // Parse CREG status
      // +CREG: <n>,<stat>
      // n: 0=disable network registration unsolicited result code, 1=enable, 2=enable with location info
      // stat: 0=not registered, 1=registered (home), 2=searching, 3=denied, 4=unknown, 5=registered (roaming)
      
      if (response.indexOf("+CREG: 0,1") >= 0 || response.indexOf("+CREG: 1,1") >= 0 || 
          response.indexOf("+CREG: 2,1") >= 0) {
        LOG_I("LTE", "Network registered (home network)");
        return true;
      }
      
      if (response.indexOf("+CREG: 0,5") >= 0 || response.indexOf("+CREG: 1,5") >= 0 || 
          response.indexOf("+CREG: 2,5") >= 0) {
        LOG_I("LTE", "Network registered (roaming)");
        return true;
      }
      
      if (response.indexOf("+CREG:") >= 0) {
        // Extract status code
        int cregPos = response.indexOf("+CREG:");
        int commaPos = response.indexOf(',', cregPos);
        if (commaPos > 0) {
          int stat = response.substring(commaPos + 1).toInt();
          
          if (stat == 0) {
            Logger::printf(LOG_INFO, "LTE", "Status: Not registered (waiting...)");
          } else if (stat == 2) {
            Logger::printf(LOG_INFO, "LTE", "Status: Searching for network...");
          } else if (stat == 3) {
            LOG_E("LTE", "Network registration denied");
            return false;
          } else if (stat == 4) {
            Logger::printf(LOG_WARN, "LTE", "Status: Unknown (waiting...)");
          }
        }
      }
    } else {
      consecutiveFailures++;
      Logger::printf(LOG_WARN, "LTE", "CREG check #%d failed (no response or timeout) - failures: %d", 
                     checkCount, consecutiveFailures);
      
      // If modem is not responding, wait longer before next attempt
      if (consecutiveFailures >= 3) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
    
    delay(2000);  // Check every 2 seconds (or longer if failures occurred)
  }
  
  Logger::printf(LOG_ERROR, "LTE", "Network registration timeout after %lu ms (%d checks)", 
                 timeout_ms, checkCount);
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
  // Note: This should be called in loop() but won't interfere with active AT commands
  // because clearSerialBuffer() is called before each command
  if (modemSerial->available()) {
    // Only read if we're not in the middle of a command
    // For now, we'll let the command handlers deal with serial data
    // This prevents interference with active operations
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
  unsigned long lastByteTime = millis();
  int bytesReceived = 0;
  
  while (millis() - startTime < timeout_ms) {
    while (modemSerial->available()) {
      char c = modemSerial->read();
      result += c;
      bytesReceived++;
      lastByteTime = millis();  // Track when we last received data
    }
    
    // If we received data recently, wait a bit more for additional data
    // This handles cases where unsolicited messages come before "OK"
    if (bytesReceived > 0 && (millis() - lastByteTime) < 100) {
      delay(10);
      continue;
    }
    
    // If no data for 200ms and we have some data, assume response is complete
    if (bytesReceived > 0 && (millis() - lastByteTime) >= 200) {
      break;
    }
    
    delay(10);
  }
  
  // Debug: show if we received any bytes at all
  if (bytesReceived > 0) {
    Logger::printf(LOG_DEBUG, "LTE", "Received %d bytes", bytesReceived);
  } else {
    Logger::printf(LOG_DEBUG, "LTE", "No data received (timeout %dms)", timeout_ms);
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

// ============================================
// HTTP POST JSON WITH BEARER TOKEN AUTH
// ============================================
bool LTEManager::httpPostJsonWithAuth(const char* url, const char* jsonBody, const char* bearerToken, String& response) {
  LOG_I("LTE", "HTTP POST JSON with Bearer auth...");
  Logger::printf(LOG_INFO, "LTE", "URL: %s", url);
  Logger::printf(LOG_INFO, "LTE", "Body: %s", jsonBody);
  
  response = "";
  
  // Initialize HTTP
  if (!httpInit()) {
    LOG_E("LTE", "HTTP init failed");
    return false;
  }
  
  // Enable SSL/TLS for HTTPS
  if (!sendATCommand("AT+HTTPSSL=1", "OK", 5000)) {
    LOG_W("LTE", "Failed to enable SSL (may not be supported)");
    // Continue anyway - some modems handle HTTPS automatically
  }
  
  // Set URL
  if (!httpSetParameter("URL", url)) {
    LOG_E("LTE", "Failed to set URL");
    httpTerminate();
    return false;
  }
  
  // Set CID
  if (!httpSetParameter("CID", "1")) {
    LOG_E("LTE", "Failed to set CID");
    httpTerminate();
    return false;
  }
  
  // Set Authorization header using USERDATA parameter
  char authHeader[256];
  snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", bearerToken);
  if (!httpSetParameter("USERDATA", authHeader)) {
    LOG_E("LTE", "Failed to set Authorization header");
    httpTerminate();
    return false;
  }
  
  // Set content type to JSON
  if (!httpSetParameter("CONTENT", "application/json")) {
    LOG_E("LTE", "Failed to set content type");
    httpTerminate();
    return false;
  }
  
  // Upload JSON body
  size_t jsonLen = strlen(jsonBody);
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,10000", jsonLen);
  
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  // Wait for DOWNLOAD prompt
  if (!waitForResponse("DOWNLOAD", 5000)) {
    LOG_E("LTE", "DOWNLOAD prompt not received");
    httpTerminate();
    return false;
  }
  
  // Send JSON data
  modemSerial->print(jsonBody);
  Logger::printf(LOG_DEBUG, "LTE", "Sent JSON: %s", jsonBody);
  
  // Wait for OK
  if (!waitForResponse("OK", 15000)) {
    LOG_E("LTE", "Failed to upload JSON data");
    httpTerminate();
    return false;
  }
  
  // Execute POST
  int statusCode, dataLength;
  if (!httpAction(HTTP_POST, &statusCode, &dataLength)) {
    LOG_E("LTE", "HTTP POST action failed");
    httpTerminate();
    return false;
  }
  
  Logger::printf(LOG_INFO, "LTE", "HTTP POST status: %d, response length: %d", statusCode, dataLength);
  
  // Read response
  if (dataLength > 0) {
    uint8_t* buffer = (uint8_t*)malloc(dataLength + 1);
    if (buffer) {
      size_t readLength = dataLength;
      if (httpRead(buffer, &readLength, dataLength)) {
        buffer[readLength] = '\0';
        response = String((char*)buffer);
        Logger::printf(LOG_INFO, "LTE", "Response: %s", response.c_str());
      } else {
        LOG_E("LTE", "Failed to read HTTP response");
      }
      free(buffer);
    } else {
      LOG_E("LTE", "Failed to allocate response buffer");
    }
  }
  
  // Terminate HTTP
  httpTerminate();
  
  if (statusCode >= 200 && statusCode < 300) {
    LOG_I("LTE", "HTTP POST JSON successful");
    return true;
  }
  
  Logger::printf(LOG_ERROR, "LTE", "HTTP POST failed with status %d", statusCode);
  return false;
}
