/*
 * ESP32 I2S Clock Diagnostic Test
 * 
 * Standalone test to verify I2S clocks are working
 * Tests BCLK and LRCLK signals
 */

#include "config.h"
#include "hardware_defs.h"
#include "logger.h"
#include "audio_manager.h"

AudioManager audio;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial connection
  
  LOG_I("Main", "");
  LOG_I("Main", "########################################");
  LOG_I("Main", "##  I2S CLOCK DIAGNOSTIC TEST ONLY  ##");
  LOG_I("Main", "########################################");
  LOG_I("Main", "");
  
  // Initialize Audio
  LOG_I("Main", "Initializing audio...");
  if (!audio.init(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_MIC_DATA, PIN_I2S_AMP_DATA)) {
    LOG_E("Main", "Audio init failed!");
    while(1) delay(1000);
  }
  
  // Start recording to enable I2S clocks
  LOG_I("Main", "Starting I2S to generate clocks...");
  if (!audio.startRecording(SAMPLE_RATE)) {
    LOG_E("Main", "Failed to start I2S!");
    while(1) delay(1000);
  }
  
  LOG_I("Main", "I2S started. Waiting for clocks to stabilize...");
  delay(1000);
  Serial.flush();
  
  LOG_I("Main", "");
  LOG_I("Main", "========================================");
  LOG_I("Main", "TESTING BCLK (GPIO 26)");
  LOG_I("Main", "========================================");
  
  // Test BCLK
  pinMode(PIN_I2S_BCLK, INPUT);
  Serial.print("BCLK readings (200 samples): ");
  int bclkHigh = 0;
  int bclkLow = 0;
  
  for (int i = 0; i < 200; i++) {
    int val = digitalRead(PIN_I2S_BCLK);
    if (val == HIGH) bclkHigh++;
    else bclkLow++;
    Serial.print(val);
    delayMicroseconds(5);
    if (i % 50 == 49) Serial.print(" ");  // Space every 50
  }
  Serial.println();
  Serial.println();
  
  Logger::printf(LOG_INFO, "Main", "BCLK Results: HIGH=%d, LOW=%d", bclkHigh, bclkLow);
  
  if (bclkHigh == 0) {
    LOG_E("Main", ">>> BCLK is stuck LOW - I2S clock NOT working! <<<");
  } else if (bclkLow == 0) {
    LOG_E("Main", ">>> BCLK is stuck HIGH - I2S clock NOT working! <<<");
  } else if (bclkHigh < 10 || bclkLow < 10) {
    LOG_W("Main", ">>> BCLK shows minimal toggling - clock may be slow/weak <<<");
  } else {
    LOG_I("Main", ">>> BCLK is toggling - clock appears to be WORKING! <<<");
  }
  
  LOG_I("Main", "");
  LOG_I("Main", "========================================");
  LOG_I("Main", "TESTING LRCLK (GPIO 25)");
  LOG_I("Main", "========================================");
  
  // Test LRCLK
  pinMode(PIN_I2S_LRCLK, INPUT);
  Serial.print("LRCLK readings (200 samples): ");
  int lrclkHigh = 0;
  int lrclkLow = 0;
  
  for (int i = 0; i < 200; i++) {
    int val = digitalRead(PIN_I2S_LRCLK);
    if (val == HIGH) lrclkHigh++;
    else lrclkLow++;
    Serial.print(val);
    delayMicroseconds(5);
    if (i % 50 == 49) Serial.print(" ");  // Space every 50
  }
  Serial.println();
  Serial.println();
  
  Logger::printf(LOG_INFO, "Main", "LRCLK Results: HIGH=%d, LOW=%d", lrclkHigh, lrclkLow);
  
  if (lrclkHigh == 0) {
    LOG_E("Main", ">>> LRCLK is stuck LOW - I2S word clock NOT working! <<<");
  } else if (lrclkLow == 0) {
    LOG_E("Main", ">>> LRCLK is stuck HIGH - I2S word clock NOT working! <<<");
  } else if (lrclkHigh < 10 || lrclkLow < 10) {
    LOG_W("Main", ">>> LRCLK shows minimal toggling - clock may be slow/weak <<<");
  } else {
    LOG_I("Main", ">>> LRCLK is toggling - word clock appears to be WORKING! <<<");
  }
  
  LOG_I("Main", "");
  LOG_I("Main", "########################################");
  LOG_I("Main", "##  DIAGNOSTIC TEST COMPLETE  ##");
  LOG_I("Main", "########################################");
  LOG_I("Main", "");
  LOG_I("Main", "Summary:");
  if (bclkHigh > 10 && bclkLow > 10 && lrclkHigh > 10 && lrclkLow > 10) {
    LOG_I("Main", ">>> BOTH CLOCKS ARE WORKING <<<");
    LOG_I("Main", "If microphone still shows constant values,");
    LOG_I("Main", "the issue is likely: wiring, power, or I2S format");
  } else {
    LOG_E("Main", ">>> CLOCKS ARE NOT WORKING PROPERLY <<<");
    LOG_E("Main", "Check: I2S driver configuration, pin conflicts, power");
  }
  
  LOG_I("Main", "");
  LOG_I("Main", "Test complete. Reset to run again.");
}

void loop() {
  // Do nothing - test runs once in setup()
  delay(1000);
}
