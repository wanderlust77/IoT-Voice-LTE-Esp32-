/*
 * ESP32 Microphone Test Firmware
 * 
 * Real-time microphone recording and logging
 * - Auto-starts recording on boot
 * - Displays all audio samples in real-time
 * - No NFC, no playback, no buttons
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
// SETUP
// ============================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(2000);  // Wait for serial connection
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "ESP32 Microphone Test");
  LOG_I("Main", "Real-time audio recording and logging");
  LOG_I("Main", "========================================");
  
  // Initialize Audio
  LOG_I("Main", "Initializing audio...");
  bool audioSuccess = audio.init(PIN_I2S_MIC_BCLK, PIN_I2S_MIC_LRCLK, PIN_I2S_MIC_DATA, 
                                 PIN_I2S_AMP_BCLK, PIN_I2S_AMP_LRCLK, PIN_I2S_AMP_DATA);
  
  if (!audioSuccess) {
    LOG_E("Main", "Audio init failed!");
    while(1) delay(1000);
  }
  
  LOG_I("Main", "Audio initialized successfully");
  
  // Start recording immediately
  LOG_I("Main", "Starting continuous recording...");
  if (!audio.startRecording(SAMPLE_RATE)) {
    LOG_E("Main", "Failed to start recording!");
    while(1) delay(1000);
  }
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "Recording started!");
  LOG_I("Main", "Speak into microphone - audio data will display below");
  LOG_I("Main", "Press RESET to stop");
  LOG_I("Main", "========================================");
  Serial.println();
}

// ============================================
// MAIN LOOP - CONTINUOUS RECORDING
// ============================================
void loop() {
  // Read audio data in chunks
  static int16_t readBuffer[256];  // Temporary buffer for reading
  size_t bytesAvailable = sizeof(readBuffer);
  uint8_t* bufferPtr = (uint8_t*)readBuffer;
  size_t bytesRead = audio.readRecordedData(bufferPtr, bytesAvailable);
  
  if (bytesRead > 0) {
    size_t samplesRead = bytesRead / sizeof(int16_t);
    static int readCount = 0;
    readCount++;
    
    // Log all audio data in real-time
    Serial.print("[DATA] Read #");
    Serial.print(readCount);
    Serial.print(" - Samples [0..");
    Serial.print(samplesRead - 1);
    Serial.print("]: ");
    
    // Display all samples
    for (size_t i = 0; i < samplesRead; i++) {
      Serial.print(readBuffer[i]);
      if (i < samplesRead - 1) Serial.print(", ");
    }
    Serial.println();
    
    // Check for constant values and warn
    bool allOnes = true;
    bool allZeros = true;
    int32_t minVal = 32767;
    int32_t maxVal = -32768;
    int64_t sumAbs = 0;
    int zeroCount = 0;
    
    for (size_t i = 0; i < samplesRead; i++) {
      int32_t val = readBuffer[i];
      if (val != 1) allOnes = false;
      if (val != 0) allZeros = false;
      if (val < minVal) minVal = val;
      if (val > maxVal) maxVal = val;
      sumAbs += (val > 0 ? val : -val);  // Absolute value
      if (val == 0) zeroCount++;
    }
    
    // Log statistics every 10 reads
    if (readCount % 10 == 0) {
      int32_t avgAbs = (samplesRead > 0) ? (sumAbs / samplesRead) : 0;
      float zeroPercent = (samplesRead > 0) ? ((float)zeroCount / samplesRead * 100.0f) : 0.0f;
      
      Logger::printf(LOG_INFO, "Main", "=== Stats (read #%d) ===", readCount);
      Logger::printf(LOG_INFO, "Main", "Range: %d to %d (max: ±32768)", minVal, maxVal);
      Logger::printf(LOG_INFO, "Main", "Avg abs: %d, Zero samples: %d (%.1f%%)", 
                    avgAbs, zeroCount, zeroPercent);
      
      if (allOnes) {
        LOG_W("Main", "⚠️  WARNING: All samples are 1 - microphone stuck!");
        LOG_W("Main", "Check: 1) Microphone power (3.3V), 2) SEL pin (GND), 3) DOUT wiring");
      } else if (allZeros) {
        LOG_W("Main", "⚠️  WARNING: All samples are 0 - microphone not sending data!");
        LOG_W("Main", "Check: 1) Microphone power (3.3V), 2) DOUT wiring (GPIO 33)");
      } else if (avgAbs < 10) {
        LOG_W("Main", "⚠️  WARNING: Very low audio levels - check microphone!");
      } else {
        float peakPercent = ((float)((maxVal > -minVal) ? maxVal : -minVal) / 32768.0f) * 100.0f;
        Logger::printf(LOG_INFO, "Main", "✅ Peak level: %.1f%% - Audio looks good!", peakPercent);
      }
      Serial.println();
    }
  } else {
    // No data available - log occasionally
    static int noDataCount = 0;
    noDataCount++;
    if (noDataCount == 1 || noDataCount % 100 == 0) {
      Logger::printf(LOG_INFO, "Main", "No I2S data available (read attempt #%d)", noDataCount);
    }
  }
  
  // Small delay to prevent tight loop
  delay(10);
}
