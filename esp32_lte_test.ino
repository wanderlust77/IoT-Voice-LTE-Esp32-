/*
 * ESP32 LTE Test Firmware
 * 
 * Milestone 2: LTE Communication and API Testing
 * 
 * Features:
 * - Initialize LTE modem
 * - Connect to network
 * - Test API: POST /voice-upload-url with Bearer token
 * - Log all responses
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - MIKROE-6287 LTE Cat-M modem (5V powered)
 */

#include "config.h"
#include "hardware_defs.h"
#include "logger.h"
#include "lte_manager.h"

// ============================================
// GLOBAL OBJECTS
// ============================================
LTEManager lte;

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "ESP32 LTE Test");
  LOG_I("Main", "Milestone 2: LTE Communication");
  LOG_I("Main", "========================================");
  
  // Initialize LTE modem
  LOG_I("Main", "Initializing LTE modem...");
  if (!lte.init(PIN_LTE_TX, PIN_LTE_RX, PIN_LTE_PWRKEY, PIN_LTE_RESET, LTE_BAUD_RATE)) {
    LOG_E("Main", "LTE init failed!");
    while(1) delay(1000);  // Halt on error
  }
  
  // Power on modem
  LOG_I("Main", "Powering on LTE modem...");
  if (!lte.powerOn()) {
    LOG_E("Main", "Failed to power on modem!");
    LOG_E("Main", "Check: 1) Modem power (5V), 2) UART wiring, 3) TX/RX not swapped");
    while(1) delay(1000);  // Halt on error
  }
  
#if !defined(LTE_SKIP_NETWORK_CHECK) || (LTE_SKIP_NETWORK_CHECK == 0)
  // Check network registration (CPIN? / CREG?)
  LOG_I("Main", "Checking network registration...");
  if (!lte.checkNetwork(60000)) {  // 60 second timeout
    LOG_E("Main", "Network registration failed!");
    LOG_E("Main", "Check: 1) SIM card inserted, 2) SIM PIN correct, 3) Network coverage");
    LOG_E("Main", "");
    LOG_W("Main", "Continuing anyway for API testing (may fail without network)...");
  }
#else
  // Skip CPIN?/CREG? - modem stays responsive; go straight to APN/bearer
  LOG_I("Main", "Skipping network check (LTE_SKIP_NETWORK_CHECK=1)...");
  delay(1500);  // Brief settle after powerOn
#endif
  
  // Configure bearer APN
  LOG_I("Main", "Configuring bearer APN...");
  if (!lte.configureBearerAPN(LTE_APN)) {
    LOG_E("Main", "Failed to configure APN!");
    while(1) delay(1000);  // Halt on error
  }
  
  // Open bearer connection
  LOG_I("Main", "Opening bearer connection...");
  if (!lte.openBearer()) {
    LOG_E("Main", "Failed to open bearer!");
    while(1) delay(1000);  // Halt on error
  }
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "LTE connection established!");
  LOG_I("Main", "Testing API: POST /voice-upload-url");
  LOG_I("Main", "========================================");
  
  // Test API: Request upload URL
  testVoiceUploadUrl();
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "Test complete!");
  LOG_I("Main", "========================================");
}

// ============================================
// TEST VOICE UPLOAD URL API
// ============================================
void testVoiceUploadUrl() {
  // Build full URL
  // NOTE: Testing with HTTP first to verify basic flow, then switch to HTTPS
  char url[256];
  snprintf(url, sizeof(url), "http://httpbin.org/post");  // Test endpoint
  
  LOG_W("Main", "========================================");
  LOG_W("Main", "TESTING WITH HTTP (httpbin.org) FIRST");
  LOG_W("Main", "Once this works, we'll switch to HTTPS");
  LOG_W("Main", "========================================");
  
  // Build JSON request body
  // Example: token_uid from NFC (using test value for now)
  char jsonBody[256];
  snprintf(jsonBody, sizeof(jsonBody), 
           "{\"content_type\":\"audio/m4a\",\"file_ext\":\"m4a\",\"duration_ms\":5200,\"token_uid\":\"04A1B2C3D4\"}");
  
  LOG_I("Main", "Sending POST request...");
  Logger::printf(LOG_INFO, "Main", "URL: %s", url);
  Logger::printf(LOG_INFO, "Main", "Body: %s", jsonBody);
  Logger::printf(LOG_INFO, "Main", "Token: %s", DEVICE_TOKEN);
  
  // Make POST request
  String response;
  bool success = lte.httpPostJsonWithAuth(url, jsonBody, DEVICE_TOKEN, response);
  
  if (success) {
    LOG_I("Main", "========================================");
    LOG_I("Main", "API Request Successful!");
    LOG_I("Main", "========================================");
    Logger::printf(LOG_INFO, "Main", "Response: %s", response.c_str());
    
    // Parse and display response fields
    // Expected: {"ok":true,"storage_path":"...","upload_url":"...","recipient_id":"...","sender_id":"..."}
    if (response.indexOf("\"ok\":true") >= 0) {
      LOG_I("Main", "Response indicates success (ok: true)");
    }
    
    // Extract upload_url if present
    int urlStart = response.indexOf("\"upload_url\":\"");
    if (urlStart >= 0) {
      urlStart += 14;  // Skip "upload_url":"
      int urlEnd = response.indexOf("\"", urlStart);
      if (urlEnd > urlStart) {
        String uploadUrl = response.substring(urlStart, urlEnd);
        Logger::printf(LOG_INFO, "Main", "Upload URL: %s", uploadUrl.c_str());
      }
    }
    
    // Extract storage_path if present
    int pathStart = response.indexOf("\"storage_path\":\"");
    if (pathStart >= 0) {
      pathStart += 15;  // Skip "storage_path":"
      int pathEnd = response.indexOf("\"", pathStart);
      if (pathEnd > pathStart) {
        String storagePath = response.substring(pathStart, pathEnd);
        Logger::printf(LOG_INFO, "Main", "Storage Path: %s", storagePath.c_str());
      }
    }
    
    // Extract recipient_id if present
    int recipientStart = response.indexOf("\"recipient_id\":\"");
    if (recipientStart >= 0) {
      recipientStart += 16;  // Skip "recipient_id":"
      int recipientEnd = response.indexOf("\"", recipientStart);
      if (recipientEnd > recipientStart) {
        String recipientId = response.substring(recipientStart, recipientEnd);
        Logger::printf(LOG_INFO, "Main", "Recipient ID: %s", recipientId.c_str());
      }
    }
    
    // Extract sender_id if present
    int senderStart = response.indexOf("\"sender_id\":\"");
    if (senderStart >= 0) {
      senderStart += 13;  // Skip "sender_id":"
      int senderEnd = response.indexOf("\"", senderStart);
      if (senderEnd > senderStart) {
        String senderId = response.substring(senderStart, senderEnd);
        Logger::printf(LOG_INFO, "Main", "Sender ID: %s", senderId.c_str());
      }
    }
    
  } else {
    LOG_E("Main", "========================================");
    LOG_E("Main", "API Request Failed!");
    LOG_E("Main", "========================================");
    Logger::printf(LOG_ERROR, "Main", "Response: %s", response.c_str());
    LOG_E("Main", "Check: 1) Network connection, 2) API URL correct, 3) Device token valid");
  }
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // Update LTE manager (process incoming data)
  lte.update();
  
  // Nothing else to do - test runs once in setup()
  delay(1000);
}
