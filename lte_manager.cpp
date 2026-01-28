#include "lte_manager.h"
#include "logger.h"
#include <Arduino.h>

// ============================================
// CONSTRUCTOR
// ============================================
LTEManager::LTEManager(uint8_t rxPin, uint8_t txPin, uint8_t pwrPin, unsigned long baudRate)
  : rxPin(rxPin), txPin(txPin), pwrPin(pwrPin), baudRate(baudRate), modemSerial(nullptr) {
}

// ============================================
// INITIALIZATION
// ============================================
bool LTEManager::begin() {
  LOG_I("LTE", "Initializing LTE Manager...");
  
  // Initialize serial port for modem
  modemSerial = new HardwareSerial(1);
  modemSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
  
  // Setup power pin
  pinMode(pwrPin, OUTPUT);
  digitalWrite(pwrPin, LOW);
  
  LOG_I("LTE", "LTE Manager initialized");
  return true;
}

// ============================================
// POWER CONTROL
// ============================================
bool LTEManager::powerOn() {
  LOG_I("LTE", "Checking if modem is already on...");
  
  // Increase initial check timeout to 3s
  if (sendATCommand("AT", "OK", 3000)) {
    LOG_I("LTE", "Modem already on");
    return true;
  }
  
  LOG_I("LTE", "Modem off, powering on...");
  
  // Power cycle: PWR_KEY pulse
  digitalWrite(pwrPin, HIGH);
  delay(1200);  // SIM7070 needs ~1s pulse
  digitalWrite(pwrPin, LOW);
  
  // Wait for boot sequence
  LOG_I("LTE", "Waiting for modem boot...");
  
  // SIM7070 can take 5-20 seconds to fully boot
  // During boot, it sends unsolicited responses like:
  //   +CFUN: 1
  //   +CPIN: READY
  //   SMS Ready
  // We need to wait for these and then send AT to get OK
  
  // Wait up to 30 seconds total
  unsigned long bootStart = millis();
  int attempts = 15;  // Check every 2 seconds
  
  while (attempts > 0) {
    delay(2000);  // Check interval
    
    // Clear any boot messages
    clearSerialBuffer();
    
    // Try sending AT command
    modemSerial->println("AT");
    Logger::printf(LOG_DEBUG, "LTE", "TX: AT (boot check)");
    
    // Read response (with 3s timeout)
    String response = readSerial(3000);
    Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
    
    // Check if we got OK in the response
    if (response.indexOf("OK") >= 0) {
      LOG_I("LTE", "Modem boot complete");
      return true;
    }
    
    // Check for specific boot messages
    if (response.indexOf("READY") >= 0 || response.indexOf("+CFUN:") >= 0) {
      LOG_I("LTE", "Modem booting (received status message)...");
    } else if (response.length() == 0) {
      LOG_W("LTE", "No response from modem yet...");
    }
    
    attempts--;
    
    if (millis() - bootStart > 30000) {
      LOG_E("LTE", "Modem boot timeout (30s)");
      return false;
    }
  }
  
  LOG_E("LTE", "Failed to power on modem");
  return false;
}

void LTEManager::powerOff() {
  LOG_I("LTE", "Powering off modem...");
  sendATCommand("AT+CPOWD=1", "OK", 5000);
  delay(2000);
}

// ============================================
// NETWORK OPERATIONS
// ============================================
bool LTEManager::checkNetwork(unsigned long timeout) {
  LOG_I("LTE", "Checking network registration...");
  
  // Enable unsolicited network registration notifications
  // AT+CREG=2: Enable network registration and location info URC
  if (!sendATCommand("AT+CREG=2", "OK", 5000)) {
    LOG_W("LTE", "Failed to enable CREG notifications");
  }
  
  // Check signal strength first
  String csqResp;
  if (sendATCommandGetResponse("AT+CSQ", csqResp, 5000)) {
    Logger::printf(LOG_INFO, "LTE", "Signal check response: %s", csqResp.c_str());
    
    // Parse CSQ response: +CSQ: <rssi>,<ber>
    int csqPos = csqResp.indexOf("+CSQ:");
    if (csqPos >= 0) {
      int rssi = -1;
      int ber = -1;
      sscanf(csqResp.c_str() + csqPos, "+CSQ: %d,%d", &rssi, &ber);
      
      if (rssi == 99) {
        LOG_W("LTE", "No signal detected (CSQ: 99,99)");
        LOG_W("LTE", "Check antenna connection!");
      } else if (rssi >= 0 && rssi < 10) {
        Logger::printf(LOG_WARN, "LTE", "Weak signal (RSSI: %d)", rssi);
      } else if (rssi >= 10) {
        Logger::printf(LOG_INFO, "LTE", "Good signal (RSSI: %d)", rssi);
      }
    }
  }
  
  // Check registration status
  unsigned long startTime = millis();
  int consecutiveFailures = 0;
  const int maxConsecutiveFailures = 5;
  
  while (millis() - startTime < timeout) {
    String response;
    if (sendATCommandGetResponse("AT+CREG?", response, 5000)) {
      Logger::printf(LOG_DEBUG, "LTE", "CREG response: %s", response.c_str());
      consecutiveFailures = 0;  // Reset counter on successful response
      
      // Parse response: +CREG: <n>,<stat>[,<lac>,<ci>]
      // stat: 0=not registered, 1=registered home, 2=searching, 3=denied, 4=unknown, 5=registered roaming
      int cregPos = response.indexOf("+CREG:");
      if (cregPos >= 0) {
        int n, stat;
        sscanf(response.c_str() + cregPos, "+CREG: %d,%d", &n, &stat);
        
        if (stat == 1 || stat == 5) {
          LOG_I("LTE", "Network registered!");
          return true;
        } else if (stat == 2) {
          LOG_I("LTE", "Searching for network...");
        } else if (stat == 3) {
          LOG_E("LTE", "Registration denied");
          return false;
        } else if (stat == 0) {
          LOG_W("LTE", "Not registered (searching disabled?)");
        }
      }
    } else {
      consecutiveFailures++;
      Logger::printf(LOG_WARN, "LTE", "AT+CREG? failed (attempt %d/%d)", consecutiveFailures, maxConsecutiveFailures);
      
      if (consecutiveFailures >= maxConsecutiveFailures) {
        LOG_E("LTE", "Modem not responding to CREG queries");
        
        // Try a simple AT command to check if modem is still alive
        if (sendATCommand("AT", "OK", 3000)) {
          LOG_W("LTE", "Modem still responsive, continuing...");
          consecutiveFailures = 0;
        } else {
          LOG_E("LTE", "Modem not responding at all!");
          return false;
        }
      }
    }
    
    delay(2000);  // Check every 2 seconds
  }
  
  LOG_E("LTE", "Network registration timeout");
  return false;
}

bool LTEManager::configureBearerAPN(const char* apn) {
  LOG_I("LTE", "Configuring APN...");
  Logger::printf(LOG_INFO, "LTE", "APN: %s", apn);
  
  // SIM7070E uses AT+CGDCONT instead of SAPBR
  // Format: AT+CGDCONT=<cid>,"<PDP_type>","<APN>"
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
  
  if (!sendATCommand(cmd, "OK", 5000)) {
    LOG_E("LTE", "Failed to configure APN!");
    return false;
  }
  
  LOG_I("LTE", "APN configured");
  return true;
}

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
// HTTP POST JSON WITH BEARER TOKEN AUTH
// ============================================
bool LTEManager::httpPostJsonWithAuth(const char* url, const char* jsonBody, const char* bearerToken, String& response) {
  LOG_I("LTE", "HTTP POST JSON with Bearer auth (trying HTTPINIT method)...");
  Logger::printf(LOG_INFO, "LTE", "URL: %s", url);
  Logger::printf(LOG_INFO, "LTE", "Body: %s", jsonBody);
  
  response = "";
  
  // Try the older, simpler HTTP command set (HTTPINIT, HTTPPARA, HTTPACTION)
  // This is more widely supported and doesn't require SHCONN
  
  // 1. Initialize HTTP service
  LOG_I("LTE", "Initializing HTTP service...");
  if (!sendATCommand("AT+HTTPINIT", "OK", 5000)) {
    LOG_E("LTE", "Failed to initialize HTTP service (not supported?)");
    return false;
  }
  
  // 2. Set HTTP parameters
  char cmd[512];
  
  // Set CID (use same as PDP context)
  snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"CID\",1");
  if (!sendATCommand(cmd, "OK", 5000)) {
    LOG_E("LTE", "Failed to set CID");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  // Set URL
  snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!sendATCommand(cmd, "OK", 5000)) {
    LOG_E("LTE", "Failed to set URL");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  LOG_I("LTE", "URL configured");
  
  // Set Content-Type
  if (!sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK", 5000)) {
    LOG_E("LTE", "Failed to set content type");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  // 3. Set custom headers (Authorization)
  // Format: AT+HTTPPARA="USERDATA","Authorization: Bearer token"
  char authHeader[300];
  snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", bearerToken);
  snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"USERDATA\",\"%s\"", authHeader);
  if (!sendATCommand(cmd, "OK", 5000)) {
    LOG_W("LTE", "Failed to set Authorization header (may not be supported)");
    // Continue anyway - some modems don't support USERDATA
  }
  
  // 4. Send POST data
  int bodyLen = strlen(jsonBody);
  LOG_I("LTE", "Sending POST body...");
  
  clearSerialBuffer();
  snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,10000", bodyLen);
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  // Wait for DOWNLOAD prompt
  unsigned long startTime = millis();
  bool downloadPrompt = false;
  while (millis() - startTime < 5000) {
    if (modemSerial->available()) {
      String resp = modemSerial->readString();
      Logger::printf(LOG_DEBUG, "LTE", "RX: %s", resp.c_str());
      if (resp.indexOf("DOWNLOAD") >= 0) {
        downloadPrompt = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!downloadPrompt) {
    LOG_E("LTE", "No DOWNLOAD prompt received");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  // Send JSON body
  modemSerial->print(jsonBody);
  Logger::printf(LOG_DEBUG, "LTE", "Sent body: %s", jsonBody);
  
  // Wait for OK
  String dataResp = readSerial(10000);
  Logger::printf(LOG_DEBUG, "LTE", "Data response: %s", dataResp.c_str());
  if (dataResp.indexOf("OK") < 0) {
    LOG_E("LTE", "Failed to send POST data");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  LOG_I("LTE", "POST data sent successfully");
  
  // 5. Execute HTTP POST action
  // AT+HTTPACTION=<method>
  // method: 0=GET, 1=POST, 2=HEAD
  LOG_I("LTE", "Executing HTTP POST...");
  clearSerialBuffer();
  modemSerial->println("AT+HTTPACTION=1");
  Logger::printf(LOG_DEBUG, "LTE", "TX: AT+HTTPACTION=1");
  
  // Wait for +HTTPACTION: <method>,<status_code>,<datalen>
  // This can take a while (up to 60s)
  startTime = millis();
  int statusCode = -1;
  int dataLen = 0;
  bool actionComplete = false;
  
  while (millis() - startTime < 60000) {  // 60s timeout
    if (modemSerial->available()) {
      String resp = readSerial(10000);
      Logger::printf(LOG_DEBUG, "LTE", "Action response: %s", resp.c_str());
      
      int pos = resp.indexOf("+HTTPACTION:");
      if (pos >= 0) {
        int method, status, datalen;
        if (sscanf(resp.c_str() + pos, "+HTTPACTION: %d,%d,%d", &method, &status, &datalen) == 3) {
          statusCode = status;
          dataLen = datalen;
          actionComplete = true;
          Logger::printf(LOG_INFO, "LTE", "HTTP Response: Status=%d, DataLen=%d", statusCode, dataLen);
          break;
        }
      }
    }
    delay(100);
  }
  
  if (!actionComplete) {
    LOG_E("LTE", "HTTP POST timeout");
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  // Check status code
  if (statusCode != 200 && statusCode != 201) {
    Logger::printf(LOG_ERROR, "LTE", "HTTP error: Status code %d", statusCode);
    sendATCommand("AT+HTTPTERM", "OK", 2000);
    return false;
  }
  
  // 6. Read response data if available
  if (dataLen > 0) {
    LOG_I("LTE", "Reading HTTP response...");
    clearSerialBuffer();
    
    snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", dataLen);
    modemSerial->println(cmd);
    Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
    
    String readResp = readSerial(15000);
    Logger::printf(LOG_DEBUG, "LTE", "Read response: %s", readResp.c_str());
    
    // Parse response data
    // Format: +HTTPREAD: <datalen>\r\n<data>\r\nOK
    int dataStart = readResp.indexOf("\n") + 1;  // After +HTTPREAD: line
    int dataEnd = readResp.lastIndexOf("\r\nOK");
    if (dataEnd < 0) dataEnd = readResp.lastIndexOf("\nOK");
    
    if (dataStart > 0 && dataEnd > dataStart) {
      response = readResp.substring(dataStart, dataEnd);
      response.trim();
      Logger::printf(LOG_INFO, "LTE", "Response data: %s", response.c_str());
    }
  }
  
  // 7. Terminate HTTP service
  sendATCommand("AT+HTTPTERM", "OK", 5000);
  
  LOG_I("LTE", "HTTP POST complete");
  return true;
}

// ============================================
// AT COMMAND UTILITIES
// ============================================
bool LTEManager::sendATCommand(const char* cmd, const char* expectedResponse, unsigned long timeout) {
  clearSerialBuffer();
  
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  String response = readSerial(timeout);
  Logger::printf(LOG_DEBUG, "LTE", "Received %d bytes", response.length());
  Logger::printf(LOG_DEBUG, "LTE", "RX: %s", response.c_str());
  
  return (response.indexOf(expectedResponse) >= 0);
}

bool LTEManager::sendATCommandGetResponse(const char* cmd, String& response, unsigned long timeout) {
  clearSerialBuffer();
  
  modemSerial->println(cmd);
  Logger::printf(LOG_DEBUG, "LTE", "TX: %s", cmd);
  
  response = readSerial(timeout);
  Logger::printf(LOG_DEBUG, "LTE", "Received %d bytes", response.length());
  
  return (response.indexOf("OK") >= 0);
}

String LTEManager::readSerial(unsigned long timeout) {
  String result = "";
  unsigned long startTime = millis();
  unsigned long lastByteTime = startTime;
  
  while (millis() - startTime < timeout) {
    if (modemSerial->available()) {
      char c = modemSerial->read();
      result += c;
      lastByteTime = millis();
    }
    
    // If we've received data and there's been no new data for 200ms, 
    // assume the response is complete
    if (result.length() > 0 && (millis() - lastByteTime > 200)) {
      break;
    }
    
    delay(10);
  }
  
  return result;
}

void LTEManager::clearSerialBuffer() {
  while (modemSerial->available()) {
    modemSerial->read();
  }
}

bool LTEManager::waitForResponse(const char* expected, unsigned long timeout) {
  unsigned long startTime = millis();
  String buffer = "";
  
  while (millis() - startTime < timeout) {
    if (modemSerial->available()) {
      char c = modemSerial->read();
      buffer += c;
      
      if (buffer.indexOf(expected) >= 0) {
        return true;
      }
      
      // Limit buffer size
      if (buffer.length() > 1024) {
        buffer = buffer.substring(buffer.length() - 512);
      }
    }
    delay(10);
  }
  
  return false;
}
