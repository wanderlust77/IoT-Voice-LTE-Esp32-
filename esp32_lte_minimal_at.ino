/*
 * ESP32 LTE Minimal AT Commands
 * 
 * Direct AT command implementation - no TinyGSM
 * Works with SIM7070E on MIKROE-6287 (LTE IoT 17 Click)
 */

#include "config.h"
#include "hardware_defs.h"

HardwareSerial SerialAT(2);

#define LOG(msg) Serial.printf("[%6lu] %s\n", millis(), msg)
#define LOGF(fmt, ...) Serial.printf("[%6lu] " fmt "\n", millis(), __VA_ARGS__)

// Send AT command and wait for response
String sendATCommand(const char* cmd, uint32_t timeout = 1000) {
  LOGF("TX: %s", cmd);
  
  // Clear buffer
  while (SerialAT.available()) SerialAT.read();
  
  // Send command
  SerialAT.println(cmd);
  
  // Read response
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      start = millis();  // Reset on data
    }
    delay(10);
  }
  
  LOGF("RX: %s", response.c_str());
  return response;
}

// Check if response contains expected string
bool checkResponse(String response, const char* expected) {
  return response.indexOf(expected) >= 0;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  LOG("========================================");
  LOG("ESP32 LTE Minimal AT Test");
  LOG("Direct AT Commands - No Library");
  LOG("========================================");
  
  // Init UART
  LOG("Init UART...");
  SerialAT.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
  delay(1000);
  
  // Test modem
  LOG("Testing modem...");
  String resp = sendATCommand("AT");
  if (!checkResponse(resp, "OK")) {
    LOG("ERROR: Modem not responding!");
    while(1) delay(1000);
  }
  LOG("Modem OK!");
  
  // Get modem info
  LOG("Get modem info...");
  sendATCommand("ATI");
  
  // Disable echo
  LOG("Disable echo...");
  sendATCommand("ATE0");
  
  // Check SIM
  LOG("Check SIM card...");
  resp = sendATCommand("AT+CPIN?", 2000);
  if (checkResponse(resp, "READY")) {
    LOG("SIM card ready");
  } else {
    LOG("WARNING: SIM not ready (continuing anyway)");
  }
  
  // Set APN
  LOGF("Set APN: %s", LTE_APN);
  char apnCmd[128];
  snprintf(apnCmd, sizeof(apnCmd), "AT+CGDCONT=1,\"IP\",\"%s\"", LTE_APN);
  resp = sendATCommand(apnCmd, 10000);
  if (!checkResponse(resp, "OK")) {
    LOG("WARNING: CGDCONT failed (continuing anyway)");
  }
  
  // Activate PDP context
  LOG("Activate PDP context...");
  resp = sendATCommand("AT+CGACT=1,1", 30000);
  if (checkResponse(resp, "OK") || checkResponse(resp, "ERROR")) {
    // ERROR might mean already active
    LOG("PDP command sent");
  }
  
  // Check network registration
  LOG("Check network...");
  for (int i = 0; i < 10; i++) {
    resp = sendATCommand("AT+CEREG?", 2000);
    if (checkResponse(resp, "+CEREG: 0,1") || checkResponse(resp, "+CEREG: 0,5")) {
      LOG("Network registered!");
      break;
    }
    LOGF("Waiting for network... attempt %d/10", i+1);
    delay(2000);
  }
  
  // Get signal quality
  LOG("Signal quality...");
  sendATCommand("AT+CSQ");
  
  // Get IP address
  LOG("Get IP address...");
  sendATCommand("AT+CGPADDR=1");
  
  LOG("========================================");
  LOG("Basic setup complete!");
  LOG("Modem is configured and ready");
  LOG("========================================");
  
  // Try a simple HTTP request
  LOG("Testing HTTP...");
  testHTTP();
}

void testHTTP() {
  LOG("HTTP Init...");
  String resp = sendATCommand("AT+HTTPINIT");
  if (!checkResponse(resp, "OK")) {
    LOG("HTTP already init or failed");
  }
  
  LOG("Set HTTP URL...");
  sendATCommand("AT+HTTPPARA=\"URL\",\"http://httpbin.org/get\"", 2000);
  
  LOG("Send HTTP GET request...");
  sendATCommand("AT+HTTPACTION=0", 30000);  // 0 = GET
  delay(5000);  // Wait for response
  
  LOG("Read HTTP response...");
  resp = sendATCommand("AT+HTTPREAD", 10000);
  
  LOG("HTTP Terminate...");
  sendATCommand("AT+HTTPTERM");
  
  LOG("========================================");
  LOG("HTTP test complete!");
  LOG("========================================");
}

void loop() {
  // Echo any unsolicited messages from modem
  if (SerialAT.available()) {
    Serial.write(SerialAT.read());
  }
  delay(10);
}
