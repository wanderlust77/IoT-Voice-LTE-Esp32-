/*
 * ESP32 LTE Diagnostic Tool
 * 
 * Checks SIM, signal, network mode, and provides guidance
 */

#include "config.h"
#include "hardware_defs.h"

HardwareSerial SerialAT(2);

#define LOG(msg) Serial.printf("[%6lu] %s\n", millis(), msg)
#define LOGF(fmt, ...) Serial.printf("[%6lu] " fmt "\n", millis(), __VA_ARGS__)

String sendATCommand(const char* cmd, uint32_t timeout = 2000) {
  LOGF("  TX: %s", cmd);
  while (SerialAT.available()) SerialAT.read();
  SerialAT.println(cmd);
  
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      start = millis();
    }
    delay(10);
  }
  
  LOGF("  RX: %s", response.c_str());
  return response;
}

bool checkResponse(String response, const char* expected) {
  return response.indexOf(expected) >= 0;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  LOG("========================================");
  LOG("ESP32 LTE DIAGNOSTIC TOOL");
  LOG("SIM7070E Troubleshooting");
  LOG("========================================");
  
  SerialAT.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
  delay(2000);
  
  while (SerialAT.available()) SerialAT.read();
  delay(500);
  
  LOG("");
  LOG("Testing modem...");
  bool modemOK = false;
  for (int i = 0; i < 3; i++) {
    String resp = sendATCommand("AT", 1000);
    if (checkResponse(resp, "OK")) {
      LOG("  ✓ Modem responding");
      modemOK = true;
      break;
    }
    delay(1000);
  }
  
  if (!modemOK) {
    LOG("  ✗ MODEM NOT RESPONDING!");
    LOG("  Check: Power, UART wiring");
    while(1) delay(1000);
  }
  
  // Modem info
  LOG("");
  LOG("Modem Information:");
  sendATCommand("ATI");
  sendATCommand("AT+CGMM");
  sendATCommand("AT+CGMR");
  
  // SIM Card Check
  LOG("");
  LOG("========================================");
  LOG("1. SIM CARD CHECK");
  LOG("========================================");
  String resp = sendATCommand("AT+CPIN?");
  
  if (checkResponse(resp, "READY")) {
    LOG("  ✓ SIM card detected and ready");
  } else if (checkResponse(resp, "SIM PIN")) {
    LOG("  ⚠ SIM requires PIN");
    LOG("  Action: Add PIN to config.h");
  } else if (checkResponse(resp, "ERROR")) {
    LOG("  ✗ SIM CARD NOT DETECTED!");
    LOG("");
    LOG("  Possible causes:");
    LOG("  1. No SIM card inserted");
    LOG("  2. Wrong voltage - need 1.8V LTE SIM");
    LOG("  3. SIM not seated properly");
    LOG("");
    LOG("  CRITICAL: This board supports ONLY 1.8V uSIM cards!");
    LOG("  Standard 3V SIM cards will NOT work!");
  }
  
  // Get ICCID if available
  LOG("");
  LOG("SIM Card ID:");
  sendATCommand("AT+CCID");
  
  // Network Mode
  LOG("");
  LOG("========================================");
  LOG("2. NETWORK MODE CHECK");
  LOG("========================================");
  resp = sendATCommand("AT+CNMP?");
  LOG("  Current mode:");
  if (checkResponse(resp, "2")) LOG("  Mode 2 = Automatic");
  else if (checkResponse(resp, "38")) LOG("  Mode 38 = LTE only");
  else if (checkResponse(resp, "51")) LOG("  Mode 51 = GSM only");
  
  LOG("");
  LOG("  Setting to LTE Cat-M / NB-IoT preferred...");
  sendATCommand("AT+CNMP=38");  // LTE only
  sendATCommand("AT+CMNB=1");   // Cat-M preferred
  
  // Operator Selection
  LOG("");
  LOG("========================================");
  LOG("3. OPERATOR / NETWORK");
  LOG("========================================");
  sendATCommand("AT+COPS?");
  
  // Signal Quality
  LOG("");
  LOG("========================================");
  LOG("4. SIGNAL QUALITY");
  LOG("========================================");
  resp = sendATCommand("AT+CSQ");
  
  if (checkResponse(resp, "99,99")) {
    LOG("  ✗ NO SIGNAL DETECTED!");
    LOG("");
    LOG("  Possible causes:");
    LOG("  1. No antenna connected");
    LOG("  2. No coverage in this area");
    LOG("  3. Wrong frequency bands");
  } else {
    LOG("  ✓ Signal detected");
  }
  
  // Registration Status
  LOG("");
  LOG("========================================");
  LOG("5. NETWORK REGISTRATION");
  LOG("========================================");
  
  resp = sendATCommand("AT+CREG?");
  LOG("  GSM registration:");
  if (checkResponse(resp, ",1") || checkResponse(resp, ",5")) {
    LOG("  ✓ Registered");
  } else if (checkResponse(resp, ",2")) {
    LOG("  ⚠ Searching...");
  } else {
    LOG("  ✗ Not registered");
  }
  
  resp = sendATCommand("AT+CEREG?");
  LOG("  LTE registration:");
  if (checkResponse(resp, ",1") || checkResponse(resp, ",5")) {
    LOG("  ✓ Registered");
  } else if (checkResponse(resp, ",2")) {
    LOG("  ⚠ Searching...");
  } else {
    LOG("  ✗ Not registered");
  }
  
  // Band Configuration
  LOG("");
  LOG("========================================");
  LOG("6. LTE BANDS");
  LOG("========================================");
  sendATCommand("AT+CBANDCFG?");
  
  LOG("");
  LOG("========================================");
  LOG("DIAGNOSTIC COMPLETE");
  LOG("========================================");
  LOG("");
  LOG("NEXT STEPS:");
  LOG("");
  LOG("If SIM error:");
  LOG("  1. Check SIM is inserted");
  LOG("  2. Verify it's 1.8V LTE SIM (not 3V)");
  LOG("  3. Try different SIM card");
  LOG("");
  LOG("If no signal:");
  LOG("  1. Connect LTE antenna to u.FL connector");
  LOG("  2. Check antenna placement");
  LOG("  3. Verify Cat-M coverage in your area");
  LOG("");
  LOG("If searching but not registered:");
  LOG("  1. Wait longer (can take 2-5 minutes)");
  LOG("  2. Check APN is correct for your carrier");
  LOG("  3. Verify SIM plan includes Cat-M/IoT");
  LOG("");
}

void loop() {
  if (SerialAT.available()) {
    Serial.write(SerialAT.read());
  }
  delay(10);
}
