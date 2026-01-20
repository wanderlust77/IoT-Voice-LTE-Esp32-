/*
 * logger.h
 * 
 * Centralized debug logging utility with timestamps and log levels
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ============================================
// LOG LEVELS
// ============================================
enum LogLevel {
  LOG_ERROR = 0,  // Critical errors
  LOG_WARN  = 1,  // Warnings
  LOG_INFO  = 2,  // Informational messages
  LOG_DEBUG = 3   // Debug details
};

// ============================================
// LOGGER CLASS
// ============================================
class Logger {
public:
  // Initialize logger with baud rate
  static void init(uint32_t baudRate = 115200);
  
  // Set current log level (messages below this level are filtered)
  static void setLogLevel(LogLevel level);
  
  // Print log message with level, module name, and message
  static void print(LogLevel level, const char* module, const char* message);
  
  // Print formatted log message (printf style)
  static void printf(LogLevel level, const char* module, const char* format, ...);
  
  // Print hex dump of binary data
  static void printHex(LogLevel level, const char* module, const uint8_t* data, size_t length);

private:
  static LogLevel currentLogLevel;
  static const char* getLevelString(LogLevel level);
  static unsigned long getTimestamp();
};

// ============================================
// CONVENIENCE MACROS
// ============================================
#define LOG_E(module, msg) Logger::print(LOG_ERROR, module, msg)
#define LOG_W(module, msg) Logger::print(LOG_WARN, module, msg)
#define LOG_I(module, msg) Logger::print(LOG_INFO, module, msg)
#define LOG_D(module, msg) Logger::print(LOG_DEBUG, module, msg)

#endif // LOGGER_H
