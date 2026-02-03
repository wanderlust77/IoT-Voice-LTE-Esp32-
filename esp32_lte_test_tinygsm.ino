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
// MODEM POWER CONTROL
// ============================================
void modemPowerOn() {
  LOG("LTE", "Powering on modem...");
  pinMode(PIN_LTE_PWRKEY, OUTPUT);
  pinMode(PIN_LTE_RESET, OUTPUT);
  
  // Ensure PWRKEY is high (inactive)
  digitalWrite(PIN_LTE_PWRKEY, HIGH);
  digitalWrite(PIN_LTE_RESET, HIGH);
  delay(100);
  
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
  
  // Initialize modem UART
  SerialAT.begin(LTE_BAUD_RATE, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
  
  // Power control
  modemPowerOn();
  
  LOG("LTE", "Initializing modem...");
  if (!modem.init()) {
    LOG("ERROR", "Modem init failed!");
    LOG("ERROR", "Check: 1) Power (5V), 2) UART wiring, 3) TX/RX pins");
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
