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
  clearSerialBuffer();
  for (int i = 0; i < 3; i++) {
    if (sendATCommand("AT", "OK", 1000)) {
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
  
  // Try AT command every 2 seconds for up to 15 seconds
  for (int attempt = 0; attempt < 8; attempt++) {
    delay(2000);
    Logger::printf(LOG_INFO, "LTE", "Boot check %d/8...", attempt + 1);
    
    clearSerialBuffer();
    if (sendATCommand("AT", "OK", 2000)) {
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
  } else if (pinResponse.indexOf("ERROR") >= 0) {
    LOG_W("LTE", "AT+CPIN? returned ERROR - SIM may be absent or modem not ready; continuing to CREG");
    // Do not return - try CREG anyway; modem may still register
  } else {
    Logger::printf(LOG_ERROR, "LTE", "Unexpected SIM status: %s", pinResponse.c_str());
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
        // Signal strength check: +CSQ: rssi,ber (rssi 0-31 = signal, 99 = no signal)
        String csqResp;
        if (sendATCommandGetResponse("AT+CSQ", csqResp, 5000)) {
          int rssi = -1, ber = -1;
          int pos = csqResp.indexOf("+CSQ:");
          if (pos >= 0 && sscanf(csqResp.c_str() + pos, "+CSQ: %d,%d", &rssi, &ber) >= 1) {
            if (rssi == 99) {
              LOG_W("LTE", "Signal: no signal (CSQ 99)");
            } else if (rssi >= 0 && rssi <= 31) {
              Logger::printf(LOG_INFO, "LTE", "Signal: CSQ %d (0=weak, 31=strong)", rssi);
            }
          }
        }
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
// WAIT FOR MODEM READY (RF + SIM)
// ============================================
// SIM7070E can answer AT but drop CGDCONT until +CFUN:1. Poll until CFUN:1.
// CPIN READY is optional - if CPIN? returns ERROR we still proceed when CFUN:1.
bool LTEManager::waitForModemReady(uint32_t timeout_ms) {
  LOG_I("LTE", "Waiting for modem RF readiness (+CFUN: 1)...");
  unsigned long start = millis();
  const unsigned long pollInterval = 2000;
  const unsigned long readTimeout = 5000;
  int pollCount = 0;
  
  while (millis() - start < timeout_ms) {
    pollCount++;
    clearSerialBuffer();
    
    modemSerial->println("AT+CFUN?");
    Logger::printf(LOG_DEBUG, "LTE", "TX: AT+CFUN? (poll %d)", pollCount);
    String cfunResp = readSerial(readTimeout);
    if (cfunResp.indexOf("+CFUN: 1") >= 0 || cfunResp.indexOf("+CFUN:1") >= 0) {
      LOG_I("LTE", "RF ready (+CFUN: 1)");
      LOG_I("LTE", "Modem ready for APN config");
      return true;
    }
    delay(pollInterval);
  }
  
  Logger::printf(LOG_ERROR, "LTE", "Modem RF not ready (+CFUN: 1) within %lu ms", timeout_ms);
  return false;
}

// ============================================
// CONFIGURE BEARER APN
// ============================================
bool LTEManager::configureBearerAPN(const char* apn) {
  LOG_I("LTE", "Configuring APN...");
  Logger::printf(LOG_INFO, "LTE", "APN: %s", apn);
  
  // Gate: wait until RF ready (+CFUN:1) so SIM7070E does not drop CGDCONT
  if (!waitForModemReady(30000)) {
    LOG_E("LTE", "Aborting APN config: modem not ready");
    return false;
  }
  
  // Modem is ready (CFUN:1); drain RX, wait longer, then send CGDCONT
  // SIM7070E can stay busy briefly after CFUN? - give it 5s before CGDCONT
  clearSerialBuffer();
  delay(5000);
  
  // SIM7070E uses AT+CGDCONT (not SAPBR)
  // Format: AT+CGDCONT=<cid>,"<PDP_type>","<APN>"
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
  
  const int maxAttempts = 3;
  const uint32_t cgdcontTimeout = 15000;
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    if (attempt > 1) {
      LOG_I("LTE", "CGDCONT retry %d/%d...", attempt, maxAttempts);
      clearSerialBuffer();
      delay(3000);
    }
    if (sendATCommand(cmd, "OK", cgdcontTimeout)) {
      LOG_I("LTE", "APN configured");
      return true;
    }
  }
  
  LOG_E("LTE", "Failed to configure APN after %d attempts!", maxAttempts);
  String response;
  if (sendATCommandGetResponse("AT+CGDCONT?", response, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "Current CGDCONT: %s", response.c_str());
  }
  return false;
}

// ============================================
// OPEN BEARER CONNECTION
// ============================================
bool LTEManager::openBearer() {
  LOG_I("LTE", "Activating PDP context...");
  
  // SIM7070E uses AT+CNACT instead of SAPBR
  // Format: AT+CNACT=<pdpidx>,<action>
  // pdpidx: 0-2 (use 0)
  // action: 0=deactivate, 1=activate
  
  // First check if already active
  String checkResp;
  if (sendATCommandGetResponse("AT+CNACT?", checkResp, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "PDP check: %s", checkResp.c_str());
    // Response: +CNACT: <pdpidx>,<status>,"<ip_addr>"
    // status: 0=inactive, 1=active
    if (checkResp.indexOf("+CNACT: 0,1") >= 0) {
      LOG_I("LTE", "PDP context already active");
      return true;
    }
  }
  
  // Activate PDP context
  if (!sendATCommand("AT+CNACT=0,1", "OK", 30000)) {
    LOG_E("LTE", "Failed to activate PDP context!");
    return false;
  }
  
  // Verify activation
  delay(1000);
  if (sendATCommandGetResponse("AT+CNACT?", checkResp, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "PDP status: %s", checkResp.c_str());
    if (checkResp.indexOf("+CNACT: 0,1") >= 0) {
      LOG_I("LTE", "PDP context activated");
      return true;
    }
  }
  
  LOG_E("LTE", "PDP context activation verification failed");
  return false;
}

// ============================================
// CLOSE BEARER CONNECTION
// ============================================
bool LTEManager::closeBearer() {
  LOG_I("LTE", "Deactivating PDP context...");
  
  // SIM7070E: AT+CNACT=0,0 to deactivate
  if (!sendATCommand("AT+CNACT=0,0", "OK", 30000)) {
    LOG_E("LTE", "Failed to deactivate PDP context");
    return false;
  }
  
  LOG_I("LTE", "PDP context deactivated");
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
  int bytesReceived = 0;
  
  while (millis() - startTime < timeout_ms) {
    while (modemSerial->available()) {
      char c = modemSerial->read();
      result += c;
      bytesReceived++;
      startTime = millis();  // Reset timeout on data received
    }
    delay(10);
  }
  
  // Debug: show if we received any bytes at all
  if (bytesReceived > 0) {
    Logger::printf(LOG_DEBUG, "LTE", "Received %d bytes", bytesReceived);
    // Log hex when response is very short (helps debug single-byte / partial responses)
    if (bytesReceived <= 8 && result.length() > 0) {
      String hexStr;
      for (size_t i = 0; i < result.length(); i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", (unsigned char)result[i]);
        hexStr += buf;
      }
      Logger::printf(LOG_DEBUG, "LTE", "RX hex: %s", hexStr.c_str());
    }
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
