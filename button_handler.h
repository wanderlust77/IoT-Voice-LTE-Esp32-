/*
 * button_handler.h
 * 
 * Button input handler with debouncing and short/long press detection
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>

// ============================================
// BUTTON HANDLER CLASS
// 
// NOTE: If using GPIO 34/35/36/39 (input-only pins),
//       an external pull-up resistor (10kΩ) is REQUIRED
// ============================================
class ButtonHandler {
public:
  // Initialize button handler
  void init(uint8_t pin, uint32_t longPressMs, uint32_t debounceMs);
  
  // Update button state (call every loop iteration)
  void update();
  
  // Check if short press occurred (clears flag after reading)
  bool wasShortPress();
  
  // Check if long press occurred (clears flag after reading)
  bool wasLongPress();
  
  // Check if button is currently pressed
  bool isCurrentlyPressed();
  
  // Get current press duration in milliseconds
  uint32_t getCurrentPressDuration();

private:
  uint8_t buttonPin;
  uint32_t longPressThreshold;
  uint32_t debounceDelay;
  
  // State tracking
  bool currentState;           // Current debounced button state
  bool lastRawState;           // Last raw reading
  unsigned long lastDebounceTime;
  unsigned long pressStartTime;
  
  // Event flags
  bool shortPressFlag;
  bool longPressFlag;
  bool longPressTriggered;     // Prevents multiple long press triggers
  
  // Helper functions
  bool readRawState();
};

#endif // BUTTON_HANDLER_H
