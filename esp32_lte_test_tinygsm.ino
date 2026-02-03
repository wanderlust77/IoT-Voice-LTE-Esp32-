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
#define TINY_GSM_DEBUG Serial  // Enable debug output

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
  pinMode(PIN_LTE_PWRKEY, OUTPUT);
  pinMode(PIN_LTE_RESET, OUTPUT);
  digitalWrite(PIN_LTE_PWRKEY, HIGH);
  digitalWrite(PIN_LTE_RESET, HIGH);
  delay(100);
  
  // Check if modem already responds
  if (checkModemAlive()) {
    return;  // Already on, skip power cycle
  }
  
  LOG("LTE", "Modem off, powering on...");
  
  // Pulse PWRKEY to power on (1.5s low)
  digitalWrite(PIN_LTE_PWRKEY, LOW);
  delay(1500);
  digitalWrite(PIN_LTE_PWRKEY, HIGH);
  
  LOG("LTE", "Waiting for modem to boot (15s)...");
  delay(15000);
}

void modemReset() {
  LOG("LTE", "Resetting modem...");
  digitalWrite(PIN_LTE_RESET, LOW);
  delay(200);
  digitalWrite(PIN_LTE_RESET, HIGH);
  delay(3000);
}

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
  
  // Initialize modem UART at default baud
  SerialAT.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
  
  // Power control and check if already on
  pinMode(PIN_LTE_PWRKEY, OUTPUT);
  pinMode(PIN_LTE_RESET, OUTPUT);
  digitalWrite(PIN_LTE_PWRKEY, HIGH);
  digitalWrite(PIN_LTE_RESET, HIGH);
  delay(100);
  
  // Check if modem responds at 115200 first
  bool modemAlreadyOn = false;
  LOG("LTE", "Checking if modem responds at 115200...");
  for (int i = 0; i < 3; i++) {
    SerialAT.println("AT");
    delay(500);
    if (SerialAT.available()) {
      String resp = SerialAT.readString();
      LOGF("LTE", "Response: %s", resp.c_str());
      if (resp.indexOf("OK") >= 0) {
        LOG("LTE", "Modem responding at 115200 baud!");
        modemAlreadyOn = true;
        break;
      }
    }
  }
  
  // If not responding at 115200, try other baud rates
  if (!modemAlreadyOn) {
    LOG("LTE", "Modem not at 115200, trying other baud rates...");
    uint32_t detectedBaud = detectBaudRate();
    
    if (detectedBaud == 0) {
      LOG("ERROR", "Modem not responding at any baud rate");
      LOG("ERROR", "Check: 1) UART wiring, 2) TX/RX swap, 3) Modem power");
      while(1) delay(1000);
    }
    
    // Set modem to 115200 if it's at a different baud
    if (detectedBaud != 115200) {
      LOG("LTE", "Setting modem to 115200 baud...");
      SerialAT.println("AT+IPR=115200");
      delay(500);
      SerialAT.readString();  // Discard response
      
      SerialAT.updateBaudRate(115200);
      delay(500);
      
      // Verify
      SerialAT.println("AT");
      delay(500);
      String resp = SerialAT.readString();
      if (resp.indexOf("OK") >= 0) {
        LOG("LTE", "Modem now at 115200 baud");
      }
    }
  }
  
  LOG("LTE", "Initializing TinyGSM...");
  if (!modem.init()) {
    LOG("ERROR", "TinyGSM init failed!");
    LOG("ERROR", "Check debug output above for AT command responses");
    while(1) delay(1000);
  }
  
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
