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
  
  // Configure control pins (MIKROE-6287: PWRKEY and RESET are active LOW)
  pinMode(pinPwrkey, OUTPUT);
  pinMode(pinReset, OUTPUT);
  digitalWrite(pinPwrkey, HIGH);  // inactive - do not press PWRKEY
  digitalWrite(pinReset, HIGH);   // inactive - do not reset
  
  // Initialize UART (Serial2): ESP32 RX=rxPin, TX=txPin -> modem TX,RX
  modemSerial = &Serial2;
  modemSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  
  Logger::printf(LOG_INFO, "LTE", "UART: RX=GPIO%d, TX=GPIO%d, Baud=%lu", rxPin, txPin, (unsigned long)baudRate);
  
  // Let UART and modem settle
  delay(300);
  clearSerialBuffer();
  
  // Hardware reset: pulse RESET LOW then HIGH for clean state (optional, helps if modem was hung)
  digitalWrite(pinReset, LOW);
  delay(200);
  digitalWrite(pinReset, HIGH);
  delay(500);
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
  
  // First, check if modem is already powered on or booting
  clearSerialBuffer();
  for (int i = 0; i < 5; i++) {
    modemSerial->println("AT");
    Logger::printf(LOG_DEBUG, "LTE", "TX: AT (check %d/5)", i + 1);
    unsigned long atTimeout = (i < 2) ? 3500 : 2000;  // Longer for first checks (after reset)
    String resp = readSerial(atTimeout);
    Logger::printf(LOG_DEBUG, "LTE", "RX: %s", resp.c_str());
    
    if (resp.indexOf("OK") >= 0) {
      powered = true;
      LOG_I("LTE", "Modem already powered on");
      return true;
    }
    // Modem may be booting: unsolicited +CPIN: READY, +CFUN, SMS Ready (no OK yet)
    // Do NOT pulse PWRKEY here or we will turn the modem OFF
    if (resp.indexOf("READY") >= 0 || resp.indexOf("+CFUN") >= 0 || resp.indexOf("SMS Ready") >= 0) {
      LOG_I("LTE", "Modem booting (READY/CFUN/SMS) - waiting for OK...");
      delay(3000);
      clearSerialBuffer();
      modemSerial->println("AT");
      String retryResp = readSerial(5000);
      if (retryResp.indexOf("OK") >= 0) {
        powered = true;
        LOG_I("LTE", "Modem ready after wait");
        return true;
      }
    }
    delay(500);
  }
  
  // No OK and no boot messages - modem is off or hung
  LOG_I("LTE", "Modem off or hung - power cycling...");
  
  // Full power cycle: 1) Turn off (PWRKEY LOW 1.5s), 2) Wait, 3) Turn on (PWRKEY LOW 1.5s)
  digitalWrite(pinPwrkey, LOW);
  delay(1500);
  digitalWrite(pinPwrkey, HIGH);
  delay(2000);
  digitalWrite(pinPwrkey, LOW);
  delay(1500);
  digitalWrite(pinPwrkey, HIGH);
  
  LOG_I("LTE", "Waiting for modem boot (up to 20s)...");
  
  for (int attempt = 0; attempt < 10; attempt++) {
    delay(2000);
    Logger::printf(LOG_INFO, "LTE", "Boot check %d/10...", attempt + 1);
    
    clearSerialBuffer();
    modemSerial->println("AT");
    String resp = readSerial(3000);
    if (resp.indexOf("OK") >= 0) {
      powered = true;
      Logger::printf(LOG_INFO, "LTE", "Modem responded after %d seconds", (attempt + 1) * 2);
      return true;
    }
    if (resp.indexOf("READY") >= 0 || resp.indexOf("+CFUN") >= 0) {
      LOG_I("LTE", "Modem booting, waiting...");
      delay(2000);
      clearSerialBuffer();
      modemSerial->println("AT");
      if (readSerial(3000).indexOf("OK") >= 0) {
        powered = true;
        LOG_I("LTE", "Modem ready");
        return true;
      }
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
  
  // Modem can go busy briefly after CPIN; drain RX and wait before CREG
  delay(2000);
  clearSerialBuffer();
  delay(1000);
  
  // Sanity check: modem still responsive
  if (!sendATCommand("AT", "OK", 3000)) {
    LOG_W("LTE", "Modem not responding after CPIN - retrying AT...");
    delay(2000);
    clearSerialBuffer();
    if (!sendATCommand("AT", "OK", 5000)) {
      LOG_E("LTE", "Modem unresponsive after CPIN check");
      return false;
    }
  }
  
  // Wait for network registration
  unsigned long startTime = millis();
  while (millis() - startTime < timeout_ms) {
    String response;
    if (sendATCommandGetResponse("AT+CREG?", response, 8000)) {
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
  LOG_I("LTE", "Configuring APN...");
  Logger::printf(LOG_INFO, "LTE", "APN: %s", apn);
  
  // Brief settle and drain any unsolicited (e.g. SMS Ready)
  clearSerialBuffer();
  delay(500);
  
  // Check modem responsiveness; accept OK or unsolicited (SMS Ready, READY, +CFUN)
  modemSerial->println("AT");
  String atResp = readSerial(3000);
  bool responsive = (atResp.indexOf("OK") >= 0) ||
                    (atResp.indexOf("SMS Ready") >= 0) ||
                    (atResp.indexOf("READY") >= 0) ||
                    (atResp.indexOf("+CFUN") >= 0);
  if (!responsive) {
    LOG_E("LTE", "Modem not responding before APN config");
    return false;
  }
  if (atResp.indexOf("OK") < 0) {
    LOG_I("LTE", "Modem sent unsolicited - sending AT again");
    clearSerialBuffer();
    modemSerial->println("AT");
    readSerial(3000);  // drain
  }
  
  // SIM7070E uses AT+CGDCONT instead of SAPBR
  // Format: AT+CGDCONT=<cid>,"<PDP_type>","<APN>"
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
  
  if (!sendATCommand(cmd, "OK", 10000)) {
    LOG_E("LTE", "Failed to configure APN!");
    String response;
    if (sendATCommandGetResponse("AT+CGDCONT?", response, 5000)) {
      Logger::printf(LOG_INFO, "LTE", "Current CGDCONT: %s", response.c_str());
    }
    return false;
  }
  
  LOG_I("LTE", "APN configured");
  return true;
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
