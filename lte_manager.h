/*
 * lte_manager.h
 * 
 * LTE modem manager for AT commands and HTTP operations
 */

#ifndef LTE_MANAGER_H
#define LTE_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>

// ============================================
// HTTP METHOD
// ============================================
enum HttpMethod {
  HTTP_GET = 0,
  HTTP_POST = 1
};

// ============================================
// LTE MANAGER CLASS
// ============================================
class LTEManager {
public:
  // Initialize LTE manager and UART
  bool init(uint8_t txPin, uint8_t rxPin, uint8_t pwrkeyPin, uint8_t resetPin, uint32_t baudRate);
  
  // Power on modem (pulse PWRKEY)
  bool powerOn();
  
  // Power off modem
  bool powerOff();
  
  // Check network registration
  bool checkNetwork(uint32_t timeout_ms);
  
  // Configure bearer profile with APN
  bool configureBearerAPN(const char* apn);
  
  // Open bearer connection
  bool openBearer();
  
  // Close bearer connection
  bool closeBearer();
  
  // HTTP GET request
  // Returns true if successful, fills buffer with response data
  bool httpGet(const char* url, uint8_t* buffer, size_t* length, size_t maxLength);
  
  // HTTP POST request
  // Returns true if successful
  bool httpPost(const char* url, const uint8_t* data, size_t length);
  
  // HTTP POST JSON with Bearer token authentication
  // Returns true if successful, fills responseBuffer with response
  bool httpPostJsonWithAuth(const char* url, const char* jsonBody, const char* bearerToken, String& response);
  
  // Update function (call in loop to process incoming data)
  void update();

private:
  HardwareSerial* modemSerial;
  uint8_t pinPwrkey;
  uint8_t pinReset;
  bool initialized;
  bool powered;
  
  // Response buffer
  String responseBuffer;
  
  // Send AT command and wait for expected response
  bool sendATCommand(const char* cmd, const char* expected, uint32_t timeout_ms);
  
  // Send AT command and get response
  bool sendATCommandGetResponse(const char* cmd, String& response, uint32_t timeout_ms);
  
  // Wait for specific response
  bool waitForResponse(const char* expected, uint32_t timeout_ms);
  
  // Clear serial buffer
  void clearSerialBuffer();
  
  // Read available serial data
  String readSerial(uint32_t timeout_ms);
  
  // HTTP helper functions
  bool httpInit();
  bool httpSetParameter(const char* param, const char* value);
  bool httpAction(HttpMethod method, int* statusCode, int* dataLength);
  bool httpRead(uint8_t* buffer, size_t* length, size_t maxLength);
  bool httpPostData(const uint8_t* data, size_t length);
  bool httpTerminate();
};

#endif // LTE_MANAGER_H
