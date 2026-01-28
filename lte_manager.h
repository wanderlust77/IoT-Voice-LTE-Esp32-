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
  // Constructor
  LTEManager(uint8_t rxPin, uint8_t txPin, uint8_t pwrPin, unsigned long baudRate);
  
  // Initialize LTE manager and UART
  bool begin();
  
  // Power on modem (pulse PWRKEY)
  bool powerOn();
  
  // Power off modem
  void powerOff();
  
  // Check network registration
  bool checkNetwork(unsigned long timeout);
  
  // Configure bearer profile with APN
  bool configureBearerAPN(const char* apn);
  
  // Open bearer connection
  bool openBearer();
  
  // Close bearer connection
  bool closeBearer();
  
  // HTTP POST JSON with Bearer token authentication
  // Returns true if successful, fills response with response data
  bool httpPostJsonWithAuth(const char* url, const char* jsonBody, const char* bearerToken, String& response);

private:
  HardwareSerial* modemSerial;
  uint8_t rxPin;
  uint8_t txPin;
  uint8_t pwrPin;
  unsigned long baudRate;
  
  // Send AT command and wait for expected response
  bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout);
  
  // Send AT command and get response
  bool sendATCommandGetResponse(const char* cmd, String& response, unsigned long timeout);
  
  // Wait for specific response
  bool waitForResponse(const char* expected, unsigned long timeout);
  
  // Clear serial buffer
  void clearSerialBuffer();
  
  // Read available serial data
  String readSerial(unsigned long timeout);
};

#endif // LTE_MANAGER_H
