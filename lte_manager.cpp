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
  LOG_I("LTE", "Checking signal strength...");
  String csqResponse;
  if (sendATCommandGetResponse("AT+CSQ", csqResponse, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "CSQ response: %s", csqResponse.c_str());
    // Parse CSQ: +CSQ: <rssi>,<ber>
    // rssi: 0-31 (higher is better), 99=unknown/no signal
    int csqPos = csqResponse.indexOf("+CSQ:");
    if (csqPos >= 0) {
      int commaPos = csqResponse.indexOf(',', csqPos);
      if (commaPos > 0) {
        int rssi = csqResponse.substring(csqPos + 6, commaPos).toInt();
        if (rssi == 99) {
          LOG_E("LTE", "========================================");
          LOG_E("LTE", "NO SIGNAL DETECTED (CSQ: 99)!");
          LOG_E("LTE", "========================================");
          LOG_E("LTE", "Possible causes:");
          LOG_E("LTE", "1. No network coverage in this area");
          LOG_E("LTE", "2. Antenna not connected properly");
          LOG_E("LTE", "3. Wrong frequency bands for your carrier");
          LOG_E("LTE", "4. SIM card not activated or wrong carrier");
          LOG_E("LTE", "5. Modem hardware issue");
          LOG_E("LTE", "========================================");
          LOG_E("LTE", "Network registration will likely fail without signal");
          LOG_E("LTE", "========================================");
        } else {
          Logger::printf(LOG_INFO, "LTE", "Signal strength: %d/31", rssi);
          if (rssi < 10) {
            LOG_W("LTE", "Weak signal (may affect registration)");
          } else if (rssi >= 20) {
            LOG_I("LTE", "Good signal strength");
          } else {
            LOG_I("LTE", "Moderate signal strength");
          }
        }
      }
    }
  } else {
    LOG_W("LTE", "Failed to read signal strength");
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

  // SIM7070E: use PDP context definition instead of SAPBR
  // Define PDP context 1 with APN
  //
  // AT+CGDCONT=1,"IP","<apn>"
  //
  // Note: Some networks require IPv4v6 ("IPV4V6"), but "IP" is widely supported.
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
  if (!sendATCommand(cmd, "OK", 10000)) {
    LOG_E("LTE", "Failed to set PDP context APN (AT+CGDCONT)");
    return false;
  }

  LOG_I("LTE", "PDP context configured with APN");
  return true;
}

// ============================================
// OPEN BEARER CONNECTION
// ============================================
bool LTEManager::openBearer() {
  LOG_I("LTE", "Opening bearer...");

  // SIM7070E: use CNACT to activate/deactivate packet data context
  //
  // Query current state:
  //   AT+CNACT?
  //   +CNACT: 0,1,"<ip>"  -> context 0 active
  //
  String response;
  if (sendATCommandGetResponse("AT+CNACT?", response, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "CNACT status: %s", response.c_str());

    // If already active (0,1) and IP is not 0.0.0.0, we are done
    int pos = response.indexOf("+CNACT:");
    if (pos >= 0) {
      int comma1 = response.indexOf(',', pos);
      int comma2 = response.indexOf(',', comma1 + 1);
      if (comma1 > 0 && comma2 > comma1) {
        int cid = response.substring(pos + 8, comma1).toInt();
        int state = response.substring(comma1 + 1, comma2).toInt();
        String ip = response.substring(comma2 + 2); // skip comma and first quote

        if (cid == 0 && state == 1 && ip.indexOf("0.0.0.0") < 0) {
          LOG_I("LTE", "PDP context already active");
          return true;
        }
      }
    }
  }

  // Activate context 0
  //   AT+CNACT=0,1
  if (!sendATCommand("AT+CNACT=0,1", "OK", 60000)) {
    LOG_E("LTE", "Failed to activate PDP context (AT+CNACT=0,1)");
    return false;
  }

  // Verify activation and log IP address
  if (sendATCommandGetResponse("AT+CNACT?", response, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "CNACT status after activate: %s", response.c_str());
  }

  LOG_I("LTE", "PDP context activated");
  return true;
}

// ============================================
// CLOSE BEARER CONNECTION
// ============================================
bool LTEManager::closeBearer() {
  LOG_I("LTE", "Deactivating PDP context...");
  // SIM7070E: deactivate context 0
  sendATCommand("AT+CNACT=0,0", "OK", 30000);
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
  LOG_I("LTE", "HTTP POST JSON with Bearer auth (SIM7070 HTTP stack)...");
  Logger::printf(LOG_INFO, "LTE", "URL: %s", url);
  Logger::printf(LOG_INFO, "LTE", "Body: %s", jsonBody);

  response = "";

  // SIM7070E HTTPS sequence (simplified):
  // 1) Ensure PDP context is active (handled by openBearer()).
  // 2) Configure HTTP stack with body/header lengths.
  // 3) Configure basic SSL for HTTPS.
  // 4) Add headers (Content-Type, Authorization).
  // 5) Send body with SHBOD.
  // 6) SHCONN, SHREQ, SHREAD, SHDISC.

  size_t jsonLen = strlen(jsonBody);

  // Configure HTTP body and header lengths
  {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"BODYLEN\",%d", (int)jsonLen);
    if (!sendATCommand(cmd, "OK", 5000)) {
      LOG_E("LTE", "Failed to set BODYLEN (AT+SHCONF)");
      return false;
    }

    // Reserve some space for headers
    snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"HEADERLEN\",350");
    if (!sendATCommand(cmd, "OK", 5000)) {
      LOG_E("LTE", "Failed to set HEADERLEN (AT+SHCONF)");
      return false;
    }
  }

  // Basic SSL configuration for HTTPS (use context 1)
  // These values are standard examples from SIMCom HTTPS app notes.
  // If any step fails, we log a warning but continue - some firmware
  // may have reasonable defaults.
  sendATCommand("AT+CSSLCFG=\"sslversion\",1,3", "OK", 5000);   // TLS 1.2
  sendATCommand("AT+CSSLCFG=\"ciphersuite\",1,0XFFFF", "OK", 5000); // Default cipher set
  sendATCommand("AT+CSSLCFG=\"seclevel\",1,0", "OK", 5000);    // No certificate validation
  sendATCommand("AT+SHSSL=1,\"\"", "OK", 5000);                // Bind HTTP to SSL ctx 1

  // Open HTTP(S) connection
  if (!sendATCommand("AT+SHCONN", "OK", 15000)) {
    LOG_E("LTE", "AT+SHCONN failed");
    sendATCommand("AT+SHDISC", "OK", 5000);
    return false;
  }

  // Add required headers
  {
    char cmd[256];
    // Content-Type: application/json
    snprintf(cmd, sizeof(cmd), "AT+SHAHEAD=\"Content-Type\",\"application/json\"");
    if (!sendATCommand(cmd, "OK", 5000)) {
      LOG_E("LTE", "Failed to add Content-Type header");
      sendATCommand("AT+SHDISC", "OK", 5000);
      return false;
    }

    // Authorization: Bearer <token>
    snprintf(cmd, sizeof(cmd), "AT+SHAHEAD=\"Authorization\",\"Bearer %s\"", bearerToken);
    if (!sendATCommand(cmd, "OK", 5000)) {
      LOG_E("LTE", "Failed to add Authorization header");
      sendATCommand("AT+SHDISC", "OK", 5000);
      return false;
    }
  }

  // Send JSON body
  {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SHBOD=%d,10000", (int)jsonLen);
    modemSerial->println(cmd);
    Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);

    // Wait for "DOWNLOAD" prompt
    if (!waitForResponse("DOWNLOAD", 5000)) {
      LOG_E("LTE", "DOWNLOAD prompt not received for SHBOD");
      sendATCommand("AT+SHDISC", "OK", 5000);
      return false;
    }

    // Send body
    modemSerial->print(jsonBody);
    Logger::printf(LOG_DEBUG, "LTE", "Sent JSON: %s", jsonBody);

    // Wait for OK
    if (!waitForResponse("OK", 15000)) {
      LOG_E("LTE", "JSON upload failed (no OK after SHBOD)");
      sendATCommand("AT+SHDISC", "OK", 5000);
      return false;
    }
  }

  // Issue HTTP POST request:
  //   AT+SHREQ="<url>",3   (3 = POST)
  {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "AT+SHREQ=\"%s\",3", url);
    modemSerial->println(cmd);
    Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  }

  // Wait for SHREQ result:
  //   +SHREQ: "<url>",<status>,<datalen>
  int statusCode = -1;
  int dataLength = 0;
  {
    String shreqResp = readSerial(60000);
    Logger::printf(LOG_DEBUG, "LTE", "SHREQ RX: %s", shreqResp.c_str());

    int pos = shreqResp.indexOf("+SHREQ:");
    if (pos >= 0) {
      // crude parse: +SHREQ: "<url>",<status>,<len>
      int firstComma = shreqResp.indexOf(',', pos);
      int secondComma = shreqResp.indexOf(',', firstComma + 1);
      if (firstComma > 0 && secondComma > firstComma) {
        statusCode = shreqResp.substring(firstComma + 1, secondComma).toInt();
        dataLength = shreqResp.substring(secondComma + 1).toInt();
      }
    }
  }

  Logger::printf(LOG_INFO, "LTE", "HTTP POST status: %d, response length: %d", statusCode, dataLength);

  // Read response body if any
  if (dataLength > 0) {
    // Read all data in one go (0,start from beginning)
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SHREAD=0,%d", dataLength);
    modemSerial->println(cmd);
    LOG_D("LTE", "TX: AT+SHREAD");

    String readResp = readSerial(30000);
    Logger::printf(LOG_DEBUG, "LTE", "SHREAD RX: %s", readResp.c_str());

    // Response format:
    //   +SHREAD: <len>
    //   <data...>
    int hdrPos = readResp.indexOf("+SHREAD:");
    if (hdrPos >= 0) {
      int nlPos = readResp.indexOf('\n', hdrPos);
      if (nlPos > 0 && (size_t)(nlPos + 1) < readResp.length()) {
        response = readResp.substring(nlPos + 1);
        Logger::printf(LOG_INFO, "LTE", "Response body: %s", response.c_str());
      }
    } else {
      LOG_W("LTE", "SHREAD header not found; raw response logged above");
    }
  }

  // Disconnect HTTP session
  sendATCommand("AT+SHDISC", "OK", 10000);

  if (statusCode >= 200 && statusCode < 300) {
    LOG_I("LTE", "HTTP POST JSON successful");
    return true;
  }

  Logger::printf(LOG_ERROR, "LTE", "HTTP POST failed with status %d", statusCode);
  return false;
}
