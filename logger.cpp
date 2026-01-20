/*
 * logger.cpp
 * 
 * Implementation of logging utility
 */

#include "logger.h"
#include <stdarg.h>

// Initialize static members
LogLevel Logger::currentLogLevel = LOG_DEBUG;

// ============================================
// INITIALIZE LOGGER
// ============================================
void Logger::init(uint32_t baudRate) {
  Serial.begin(baudRate);
  while (!Serial && millis() < 3000) {
    ; // Wait for serial port to connect (max 3 seconds)
  }
  Serial.println();
  Serial.println("===================================");
  Serial.println("ESP32 Voice LTE - Logger Initialized");
  Serial.println("===================================");
}

// ============================================
// SET LOG LEVEL
// ============================================
void Logger::setLogLevel(LogLevel level) {
  currentLogLevel = level;
}

// ============================================
// GET LOG LEVEL STRING
// ============================================
const char* Logger::getLevelString(LogLevel level) {
  switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN ";
    case LOG_INFO:  return "INFO ";
    case LOG_DEBUG: return "DEBUG";
    default:        return "?????";
  }
}

// ============================================
// GET TIMESTAMP (milliseconds since boot)
// ============================================
unsigned long Logger::getTimestamp() {
  return millis();
}

// ============================================
// PRINT LOG MESSAGE
// ============================================
void Logger::print(LogLevel level, const char* module, const char* message) {
  // Filter by log level
  if (level > currentLogLevel) {
    return;
  }
  
  // Format: [timestamp] [LEVEL] [Module] Message
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "[%10lu] [%s]", getTimestamp(), getLevelString(level));
  
  Serial.print(buffer);
  Serial.print(" [");
  Serial.print(module);
  Serial.print("] ");
  Serial.println(message);
}

// ============================================
// PRINT FORMATTED LOG MESSAGE
// ============================================
void Logger::printf(LogLevel level, const char* module, const char* format, ...) {
  // Filter by log level
  if (level > currentLogLevel) {
    return;
  }
  
  // Format timestamp and header
  char headerBuf[64];
  snprintf(headerBuf, sizeof(headerBuf), "[%10lu] [%s] [%s] ", 
           getTimestamp(), getLevelString(level), module);
  Serial.print(headerBuf);
  
  // Format message
  char msgBuf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(msgBuf, sizeof(msgBuf), format, args);
  va_end(args);
  
  Serial.println(msgBuf);
}

// ============================================
// PRINT HEX DUMP
// ============================================
void Logger::printHex(LogLevel level, const char* module, const uint8_t* data, size_t length) {
  // Filter by log level
  if (level > currentLogLevel) {
    return;
  }
  
  print(level, module, "Hex dump:");
  
  char lineBuf[80];
  for (size_t i = 0; i < length; i += 16) {
    // Print offset
    snprintf(lineBuf, sizeof(lineBuf), "  %04X: ", i);
    Serial.print(lineBuf);
    
    // Print hex values
    for (size_t j = 0; j < 16 && (i + j) < length; j++) {
      snprintf(lineBuf, sizeof(lineBuf), "%02X ", data[i + j]);
      Serial.print(lineBuf);
    }
    Serial.println();
  }
}
