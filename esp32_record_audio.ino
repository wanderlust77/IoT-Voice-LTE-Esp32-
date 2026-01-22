/*
 * ESP32 Real-Time Audio Recording
 * 
 * Purpose: Record audio continuously and display data via logs
 * 
 * Features:
 * - Continuous audio recording from SPH0645LM4H microphone
 * - Real-time audio data logging
 * - No NFC, playback, or button logic
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - SPH0645LM4H I2S microphone
 */

#include "config.h"
#include "hardware_defs.h"
#include "logger.h"
#include "audio_manager.h"

// ============================================
// GLOBAL OBJECTS
// ============================================
AudioManager audio;

// ============================================
// RECORDING BUFFER
// ============================================
#define READ_BUFFER_SIZE  256  // Samples per read
static int16_t readBuffer[READ_BUFFER_SIZE];

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "ESP32 Real-Time Audio Recording");
  LOG_I("Main", "Recording from SPH0645LM4H microphone");
  LOG_I("Main", "========================================");
  
  // Initialize Audio (recording only)
  LOG_I("Main", "Initializing audio...");
  bool audioSuccess = audio.init(PIN_I2S_MIC_BCLK, PIN_I2S_MIC_LRCLK, PIN_I2S_MIC_DATA, 
                                 PIN_I2S_AMP_BCLK, PIN_I2S_AMP_LRCLK, PIN_I2S_AMP_DATA);
  
  if (!audioSuccess) {
    LOG_E("Main", "Audio init failed!");
    LOG_E("Main", "Check: 1) I2S wiring, 2) Microphone power, 3) GPIO pins");
    while(1) delay(1000);  // Halt on error
  }
  
  LOG_I("Main", "Audio initialized successfully");
  
  // Start recording mode
  if (!audio.startRecording(SAMPLE_RATE)) {
    LOG_E("Main", "Failed to start recording mode");
    while(1) delay(1000);  // Halt on error
  }
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "Recording started - audio data will be logged");
  LOG_I("Main", "Speak into microphone to see audio data");
  LOG_I("Main", "========================================");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // Read audio data in chunks
  size_t bytesAvailable = sizeof(readBuffer);
  uint8_t* bufferPtr = (uint8_t*)readBuffer;
  size_t bytesRead = audio.readRecordedData(bufferPtr, bytesAvailable);
  
  if (bytesRead > 0) {
    size_t samplesRead = bytesRead / sizeof(int16_t);
    
    // Calculate statistics for this chunk
    int16_t minVal = readBuffer[0];
    int16_t maxVal = readBuffer[0];
    int32_t sum = 0;
    int32_t sumAbs = 0;
    
    for (size_t i = 0; i < samplesRead; i++) {
      int16_t val = readBuffer[i];
      if (val < minVal) minVal = val;
      if (val > maxVal) maxVal = val;
      sum += (int32_t)val;
      sumAbs += abs((int32_t)val);
    }
    
    int32_t avg = sum / (int32_t)samplesRead;
    int32_t avgAbs = sumAbs / (int32_t)samplesRead;
    
    // Log chunk statistics
    Logger::printf(LOG_INFO, "Main", "Chunk: samples=%d, min=%d, max=%d, avg=%d, avg_abs=%d", 
                   samplesRead, minVal, maxVal, avg, avgAbs);
    
    // Log first 10 and last 10 samples
    size_t logCount = min((size_t)10, samplesRead);
    Serial.print("[DATA] Samples [0..");
    Serial.print(logCount - 1);
    Serial.print("]: ");
    for (size_t i = 0; i < logCount; i++) {
      Serial.print(readBuffer[i]);
      if (i < logCount - 1) Serial.print(", ");
    }
    Serial.println();
    
    if (samplesRead > 20) {
      Serial.print("[DATA] Samples [");
      Serial.print(samplesRead - logCount);
      Serial.print("..");
      Serial.print(samplesRead - 1);
      Serial.print("]: ");
      for (size_t i = samplesRead - logCount; i < samplesRead; i++) {
        Serial.print(readBuffer[i]);
        if (i < samplesRead - 1) Serial.print(", ");
      }
      Serial.println();
    }
  } else {
    // No data available yet (normal in non-blocking mode)
    delay(1);
  }
  
  // Small delay to prevent tight loop
  delay(1);
}
