/*
 * button_handler.cpp
 * 
 * Implementation of button handler with debouncing
 */

#include "button_handler.h"

// ============================================
// INITIALIZE BUTTON HANDLER
// ============================================
void ButtonHandler::init(uint8_t pin, uint32_t longPressMs, uint32_t debounceMs) {
  buttonPin = pin;
  longPressThreshold = longPressMs;
  debounceDelay = debounceMs;
  
  // Configure pin as input
  // NOTE: GPIO 34 is input-only and has no internal pull-up
  // An external pull-up resistor (10kΩ to 3.3V) is required
  pinMode(buttonPin, INPUT);
  
  // Initialize state variables
  currentState = false;
  lastRawState = readRawState();
  lastDebounceTime = 0;
  pressStartTime = 0;
  
  shortPressFlag = false;
  longPressFlag = false;
  longPressTriggered = false;
}

// ============================================
// READ RAW BUTTON STATE
// ============================================
bool ButtonHandler::readRawState() {
  // Button is active LOW (pressed = LOW due to INPUT_PULLUP)
  return digitalRead(buttonPin) == LOW;
}

// ============================================
// UPDATE BUTTON STATE (non-blocking)
// ============================================
void ButtonHandler::update() {
  unsigned long now = millis();
  bool rawState = readRawState();
  
  // ========================================
  // DEBOUNCE LOGIC
  // ========================================
  // If state changed, reset debounce timer
  if (rawState != lastRawState) {
    lastDebounceTime = now;
  }
  lastRawState = rawState;
  
  // If enough time has passed, update current state
  if ((now - lastDebounceTime) > debounceDelay) {
    bool previousState = currentState;
    currentState = rawState;
    
    // ========================================
    // DETECT PRESS (rising edge)
    // ========================================
    if (currentState && !previousState) {
      // Button was just pressed
      pressStartTime = now;
      longPressTriggered = false;
    }
    
    // ========================================
    // DETECT RELEASE (falling edge)
    // ========================================
    else if (!currentState && previousState) {
      // Button was just released
      uint32_t pressDuration = now - pressStartTime;
      
      // Determine if it was short or long press
      if (!longPressTriggered) {
        if (pressDuration < longPressThreshold) {
          shortPressFlag = true;
        }
        // Note: long press flag is set below while button is held
      }
    }
  }
  
  // ========================================
  // DETECT LONG PRESS (while held)
  // ========================================
  if (currentState && !longPressTriggered) {
    uint32_t pressDuration = now - pressStartTime;
    if (pressDuration >= longPressThreshold) {
      longPressFlag = true;
      longPressTriggered = true;  // Prevent repeated triggers
    }
  }
}

// ============================================
// CHECK SHORT PRESS (clears flag)
// ============================================
bool ButtonHandler::wasShortPress() {
  if (shortPressFlag) {
    shortPressFlag = false;
    return true;
  }
  return false;
}

// ============================================
// CHECK LONG PRESS (clears flag)
// ============================================
bool ButtonHandler::wasLongPress() {
  if (longPressFlag) {
    longPressFlag = false;
    return true;
  }
  return false;
}

// ============================================
// CHECK IF CURRENTLY PRESSED
// ============================================
bool ButtonHandler::isCurrentlyPressed() {
  return currentState;
}

// ============================================
// GET CURRENT PRESS DURATION
// ============================================
uint32_t ButtonHandler::getCurrentPressDuration() {
  if (currentState) {
    return millis() - pressStartTime;
  }
  return 0;
}
