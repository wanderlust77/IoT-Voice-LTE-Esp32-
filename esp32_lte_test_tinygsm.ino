/*
 * ESP32 LTE Test with TinyGSM
 * 
 * Uses TinyGSM library for SIM7070 communication
 * Milestone 2: LTE Communication and API Testing
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - MIKROE-6287 (SIM7070E) LTE Cat-M modem
 */

#define TINY_GSM_MODEM_SIM7070
#define TINY_GSM_RX_BUFFER 1024
#define TINY_GSM_DEBUG Serial  // Re-enable to see what fails

#include <TinyGsmClient.h>
#include "config.h"
#include "hardware_defs.h"

// ============================================
// MODEM SETUP
// ============================================
HardwareSerial SerialAT(2);  // Use Serial2 for modem
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

// ============================================
// LOGGING
// ============================================
#define LOG(tag, msg) Serial.printf("[%6lu] [%-5s] %s\n", millis(), tag, msg)
#define LOGF(tag, fmt, ...) Serial.printf("[%6lu] [%-5s] " fmt "\n", millis(), tag, __VA_ARGS__)

// ============================================
// BAUD RATE AUTO-DETECTION
// ============================================
uint32_t detectBaudRate() {
  uint32_t rates[] = {9600, 115200, 57600, 38400, 19200, 460800};
  
  LOG("LTE", "Auto-detecting baud rate...");
  
  for (int i = 0; i < 6; i++) {
    uint32_t rate = rates[i];
    LOGF("LTE", "Trying %d baud...", rate);
    
    SerialAT.updateBaudRate(rate);
    delay(100);
    
    // Clear buffer
    while (SerialAT.available()) SerialAT.read();
    
    // Try AT command 3 times
    for (int attempt = 0; attempt < 3; attempt++) {
      SerialAT.println("AT");
      delay(500);
      
      if (SerialAT.available()) {
        String resp = SerialAT.readString();
        LOGF("LTE", "  Response: %s", resp.c_str());
        
        if (resp.indexOf("OK") >= 0) {
          LOGF("LTE", "Found modem at %d baud!", rate);
          return rate;
        }
      }
      delay(200);
    }
  }
  
  LOG("ERROR", "Could not detect baud rate!");
  return 0;
}

// ============================================
// MODEM POWER CONTROL
// ============================================
bool checkModemAlive() {
  LOG("LTE", "Checking if modem already on...");
  for (int i = 0; i < 3; i++) {
    SerialAT.println("AT");
    delay(500);
    if (SerialAT.available()) {
      String resp = SerialAT.readString();
      if (resp.indexOf("OK") >= 0) {
        LOG("LTE", "Modem already powered on");
        return true;
      }
    }
  }
  return false;
}

void modemPowerOn() {
  // This function is no longer used - power control is in setup()
  // Kept for compatibility with old code structure
}

// NOTE: No hardware reset available on LTE IoT 17 Click
// RST pin is ID SEL, not a reset line

// ============================================
// HTTP POST HELPER
// ============================================
bool httpPost(const char* url, const char* contentType, const char* body, 
              const char* authHeader, String& response) {
  
  LOGF("HTTP", "POST %s", url);
  LOGF("HTTP", "Body: %s", body);
  
  if (!client.connect(url, 80)) {
    LOG("HTTP", "Connection failed");
    return false;
  }
  
  // Send HTTP POST request
  client.print("POST ");
  client.print(url);
  client.println(" HTTP/1.1");
  client.println("Host: httpbin.org");
  client.print("Content-Type: ");
  client.println(contentType);
  client.print("Content-Length: ");
  client.println(strlen(body));
  
  if (authHeader && strlen(authHeader) > 0) {
    client.print("Authorization: Bearer ");
    client.println(authHeader);
  }
  
  client.println("Connection: close");
  client.println();
  client.print(body);
  
  // Read response
  unsigned long timeout = millis() + 10000;
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      char c = client.read();
      response += c;
      timeout = millis() + 1000;  // Reset timeout on data
    }
  }
  
  client.stop();
  
  LOGF("HTTP", "Response (%d bytes):", response.length());
  Serial.println(response);
  
  return response.length() > 0;
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  LOG("Main", "========================================");
  LOG("Main", "ESP32 LTE Test (TinyGSM)");
  LOG("Main", "SIM7070E Cat-M Module");
  LOG("Main", "========================================");
  
  // Setup PWRKEY pin (only control pin available - no hardware reset on this board)
  pinMode(PIN_LTE_PWRKEY, OUTPUT);
  digitalWrite(PIN_LTE_PWRKEY, HIGH);  // Inactive state
  
  LOG("LTE", "========================================");
  LOG("LTE", "IMPORTANT: Check the board LEDs:");
  LOG("LTE", "  - LD3 (STAT/Yellow): Should be ON if modem powered");
  LOG("LTE", "  - LD2 (NET/Red): Network status");
  LOG("LTE", "  - LD1 (PWR): Power indicator");
  LOG("LTE", "========================================");
  
  // Initialize UART
  LOG("LTE", "Initializing UART at 115200 baud (datasheet default)...");
  SerialAT.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
  delay(1000);
  
  // Check for any data in buffer (unsolicited messages, boot messages)
  LOG("LTE", "Checking for existing data...");
  delay(500);
  if (SerialAT.available()) {
    String existing = "";
    while (SerialAT.available()) {
      existing += (char)SerialAT.read();
    }
    LOGF("LTE", "Found data in buffer: [%s]", existing.c_str());
  }
  
  // Try AT command (modem might already be on)
  LOG("LTE", "Testing if modem responds to AT...");
  bool modemOn = false;
  for (int i = 0; i < 3; i++) {
    while (SerialAT.available()) SerialAT.read();  // Clear
    SerialAT.println("AT");
    delay(1000);
    
    String resp = "";
    String hex = "";
    while (SerialAT.available()) {
      char c = SerialAT.read();
      resp += c;
      char h[4];
      sprintf(h, "%02X ", (unsigned char)c);
      hex += h;
    }
    
    if (resp.length() > 0) {
      LOGF("LTE", "Attempt %d: [%s]", i+1, resp.c_str());
      LOGF("LTE", "  HEX: %s", hex.c_str());
      
      if (resp.indexOf("OK") >= 0) {
        LOG("LTE", "Modem is ON and responding!");
        modemOn = true;
        break;
      }
    } else {
      LOGF("LTE", "Attempt %d: No response", i+1);
    }
  }
  
  if (!modemOn) {
    LOG("LTE", "No response. Trying PWRKEY power-on...");
    digitalWrite(PIN_LTE_PWRKEY, LOW);
    delay(1500);
    digitalWrite(PIN_LTE_PWRKEY, HIGH);
    
    LOG("LTE", "Waiting for modem boot messages (15s)...");
    unsigned long bootStart = millis();
    String bootMsg = "";
    while (millis() - bootStart < 15000) {
      if (SerialAT.available()) {
        char c = SerialAT.read();
        bootMsg += c;
        Serial.print(c);  // Echo boot messages in real-time
        
        // Check for boot complete indicators
        if (bootMsg.indexOf("RDY") >= 0 || 
            bootMsg.indexOf("PB DONE") >= 0 ||
            bootMsg.indexOf("SMS Ready") >= 0) {
          Serial.println();
          LOG("LTE", "Boot message detected!");
          modemOn = true;
          break;
        }
      }
      delay(10);
    }
    
    if (bootMsg.length() > 0) {
      Serial.println();
      LOGF("LTE", "Boot messages (%d bytes): %s", bootMsg.length(), bootMsg.c_str());
    }
  }
  
  // Clear buffer
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  // Check if modem responds at 115200
  bool modemAlreadyOn = false;
  LOG("LTE", "Testing modem communication...");
  
  for (int i = 0; i < 5; i++) {
    // Clear buffer before sending
    while (SerialAT.available()) SerialAT.read();
    
    SerialAT.println("AT");
    delay(1000);
    
    String resp = "";
    String hexResp = "";
    unsigned long start = millis();
    while (millis() - start < 1000) {
      if (SerialAT.available()) {
        char c = SerialAT.read();
        resp += c;
        // Build hex representation
        char hex[4];
        sprintf(hex, "%02X ", (unsigned char)c);
        hexResp += hex;
      }
    }
    
    if (resp.length() > 0) {
      LOGF("LTE", "Attempt %d: %d bytes", i+1, resp.length());
      LOGF("LTE", "  ASCII: [%s]", resp.c_str());
      LOGF("LTE", "  HEX: %s", hexResp.c_str());
    } else {
      LOGF("LTE", "Attempt %d: No response", i+1);
    }
    
    // Check for OK or any AT command echo
    if (resp.indexOf("OK") >= 0 || resp.indexOf("OJ") >= 0 || 
        resp.indexOf("OI") >= 0 || resp.indexOf("AT") >= 0) {
      LOG("LTE", "Modem responding!");
      modemAlreadyOn = true;
      break;
    }
    
    // If we got any bytes, the UART is working but modem might be busy
    if (resp.length() > 0 && i >= 2) {
      LOG("LTE", "Got response but no OK - modem may be booting, continuing...");
      modemAlreadyOn = true;
      break;
    }
    
    delay(500);
  }
  
  // Skip all pre-configuration - let TinyGSM handle everything
  LOG("LTE", "Skipping pre-config, letting TinyGSM initialize...");
  delay(1000);
  
  // Just clear buffer
  while (SerialAT.available()) {
    SerialAT.read();
  }
  
  if (!modemAlreadyOn) {
    LOG("ERROR", "Modem not responding properly!");
    LOG("ERROR", "Possible issues:");
    LOG("ERROR", "  1) TX/RX pins swapped - try swapping GPIO 16 & 17");
    LOG("ERROR", "  2) Modem not at 115200 baud");
    LOG("ERROR", "  3) Loose connection");
    LOG("ERROR", "");
    LOG("ERROR", "Trying TX/RX swap...");
    
    // Try swapping TX/RX
    SerialAT.end();
    delay(100);
    SerialAT.begin(115200, SERIAL_8N1, PIN_LTE_TX, PIN_LTE_RX);  // SWAPPED!
    delay(500);
    
    LOG("LTE", "Testing with swapped TX/RX...");
    for (int i = 0; i < 3; i++) {
      while (SerialAT.available()) SerialAT.read();
      SerialAT.println("AT");
      delay(1000);
      
      String resp = "";
      unsigned long start = millis();
      while (millis() - start < 1000) {
        if (SerialAT.available()) {
          resp += (char)SerialAT.read();
        }
      }
      
      LOGF("LTE", "Swapped response: [%s]", resp.c_str());
      
      if (resp.indexOf("OK") >= 0) {
        LOG("LTE", "SUCCESS! TX/RX were swapped!");
        LOG("LTE", "Update your wiring or hardware_defs.h");
        modemAlreadyOn = true;
        break;
      }
    }
    
    if (!modemAlreadyOn) {
      LOG("ERROR", "Still no response. Check hardware.");
      while(1) delay(1000);
    }
  }
  
  LOG("LTE", "Initializing TinyGSM with restart...");
  LOG("LTE", "Watch for ### debug lines below:");
  
  // Give modem a moment
  delay(500);
  
  // Try restart() instead of init() - does software reset + full init
  if (!modem.restart()) {
    LOG("ERROR", "TinyGSM restart failed! Trying init()...");
    
    delay(2000);
    if (!modem.init()) {
      LOG("ERROR", "TinyGSM init also failed!");
      
      // Try manual AT to verify modem still responds
      LOG("LTE", "Verifying modem still responds...");
      SerialAT.println("AT");
      delay(500);
      if (SerialAT.available()) {
        LOG("LTE", "Modem still responds - trying to continue anyway...");
        // Don't halt - try to continue
      } else {
        LOG("ERROR", "Modem stopped responding!");
        while(1) delay(1000);
      }
    }
  }
  
  LOG("LTE", "TinyGSM initialized successfully!");
  
  // Get modem info
  String modemInfo = modem.getModemInfo();
  LOGF("LTE", "Modem: %s", modemInfo.c_str());
  
  // Set APN
  LOGF("LTE", "Setting APN: %s", LTE_APN);
  modem.gprsConnect(LTE_APN, "", "");
  
  LOG("LTE", "Waiting for network...");
  if (!modem.waitForNetwork(60000L)) {
    LOG("ERROR", "Network registration failed");
    while(1) delay(1000);
  }
  LOG("LTE", "Network registered");
  
  // Check signal quality
  int csq = modem.getSignalQuality();
  LOGF("LTE", "Signal quality: %d", csq);
  
  LOG("LTE", "Connecting to GPRS...");
  if (!modem.gprsConnect(LTE_APN, "", "")) {
    LOG("ERROR", "GPRS connection failed");
    while(1) delay(1000);
  }
  
  if (modem.isGprsConnected()) {
    LOG("LTE", "GPRS connected!");
    String ip = modem.getLocalIP();
    LOGF("LTE", "IP: %s", ip.c_str());
  } else {
    LOG("ERROR", "GPRS not connected");
    while(1) delay(1000);
  }
  
  LOG("Main", "========================================");
  LOG("Main", "Testing HTTP POST");
  LOG("Main", "========================================");
  
  // Test HTTP POST
  String response;
  char jsonBody[256];
  snprintf(jsonBody, sizeof(jsonBody), 
           "{\"content_type\":\"audio/m4a\",\"file_ext\":\"m4a\",\"duration_ms\":5200,\"token_uid\":\"04A1B2C3D4\"}");
  
  if (httpPost("http://httpbin.org/post", "application/json", jsonBody, DEVICE_TOKEN, response)) {
    LOG("Main", "HTTP POST successful!");
  } else {
    LOG("ERROR", "HTTP POST failed");
  }
  
  LOG("Main", "========================================");
  LOG("Main", "Test complete!");
  LOG("Main", "========================================");
}

// ============================================
// LOOP
// ============================================
void loop() {
  // Keep connection alive
  if (modem.isGprsConnected()) {
    // Idle
  } else {
    LOG("LTE", "GPRS disconnected, reconnecting...");
    modem.gprsConnect(LTE_APN, "", "");
  }
  
  delay(5000);
}
