/*
 * ESP32 NFC + Audio Test Firmware
 * 
 * Milestone: NFC + Audio bring-up (no LTE)
 * 
 * Features:
 * - Read NFC tag (NTAG213/215) UID
 * - Record audio on button short press
 * - Playback recorded audio on button long press
 * - All local (no network/LTE)
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - PN532 NFC reader (I2C)
 * - SPH0645LM4H I2S microphone
 * - MAX98357A I2S amplifier
 * - Button on GPIO34 (requires external pull-up!)
 */

#include "config.h"
#include "hardware_defs.h"
#include "app_state.h"
#include "logger.h"
#include "button_handler.h"
#include "nfc_manager.h"
#include "audio_manager.h"

// ============================================
// GLOBAL OBJECTS
// ============================================
ButtonHandler button;
NFCManager nfc;
AudioManager audio;

// ============================================
// STATE MACHINE
// ============================================
enum TestState {
  STATE_IDLE,
  STATE_NFC_READ,
  STATE_RECORDING,
  STATE_PLAYING,
  STATE_ERROR
};

TestState currentState = STATE_IDLE;
ErrorCode lastError = ERR_NONE;

// ============================================
// AUDIO BUFFER (LOCAL STORAGE)
// ============================================
#define MAX_AUDIO_SAMPLES  (SAMPLE_RATE * 5)  // 5 seconds max at 16kHz
int16_t* audioBuffer = nullptr;
size_t audioBufferSize = 0;
size_t recordedSamples = 0;

// ============================================
// NFC DATA
// ============================================
uint8_t nfcUID[7];
uint8_t nfcUIDLength = 0;

// ============================================
// TIMING
// ============================================
unsigned long recordingStartTime = 0;
unsigned long playbackStartTime = 0;

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "ESP32 NFC + Audio Test");
  LOG_I("Main", "Milestone: Local bring-up (no LTE)");
  LOG_I("Main", "========================================");
  
  // Allocate audio buffer
  LOG_I("Main", "Allocating audio buffer...");
  audioBuffer = (int16_t*)malloc(MAX_AUDIO_SAMPLES * sizeof(int16_t));
  if (!audioBuffer) {
    LOG_E("Main", "Failed to allocate audio buffer!");
    currentState = STATE_ERROR;
    return;
  }
  audioBufferSize = MAX_AUDIO_SAMPLES;
  Logger::printf(LOG_INFO, "Main", "Audio buffer: %d samples (%.1f seconds max)", 
                 audioBufferSize, (float)audioBufferSize / SAMPLE_RATE);
  
  // Initialize button
  LOG_I("Main", "Initializing button...");
  if (!button.init(PIN_BUTTON)) {
    LOG_E("Main", "Button init failed!");
    currentState = STATE_ERROR;
    return;
  }
  LOG_I("Main", "NOTE: GPIO34 requires EXTERNAL pull-up resistor!");
  
  // Initialize NFC
  LOG_I("Main", "Initializing NFC...");
  if (!nfc.init(PIN_NFC_IRQ, PIN_NFC_RST)) {
    LOG_E("Main", "NFC init failed!");
    LOG_E("Main", "Check: 1) I2C wiring, 2) PN532 power, 3) I2C address");
    currentState = STATE_ERROR;
    return;
  }
  
  // Initialize Audio
  LOG_I("Main", "Initializing audio...");
  if (!audio.init(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_MIC_DATA, PIN_I2S_AMP_DATA, SAMPLE_RATE)) {
    LOG_E("Main", "Audio init failed!");
    currentState = STATE_ERROR;
    return;
  }
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "Initialization complete!");
  LOG_I("Main", "");
  LOG_I("Main", "Usage:");
  LOG_I("Main", "1. Present NFC tag to reader");
  LOG_I("Main", "2. SHORT press button → Record 3 seconds");
  LOG_I("Main", "3. LONG press button → Playback recording");
  LOG_I("Main", "========================================");
  
  logHeapStatus();
  currentState = STATE_IDLE;
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // Update button state
  button.update();
  
  // Check for button actions
  ButtonAction action = button.getAction();
  
  // State machine
  switch (currentState) {
    
    // ----------------------------------------
    // IDLE STATE
    // ----------------------------------------
    case STATE_IDLE: {
      // Check for NFC tag periodically (non-blocking)
      static unsigned long lastNFCCheck = 0;
      if (millis() - lastNFCCheck > 500) {  // Check every 500ms
        lastNFCCheck = millis();
        
        if (nfc.readUID(nfcUID, &nfcUIDLength, 0)) {  // 0 = non-blocking
          // NFC tag detected!
          char uidStr[32];
          nfc.formatUID(nfcUID, nfcUIDLength, uidStr, sizeof(uidStr));
          LOG_I("Main", "========================================");
          Logger::printf(LOG_INFO, "Main", "NFC Tag Detected: %s", uidStr);
          LOG_I("Main", "Press button to record or playback");
          LOG_I("Main", "========================================");
          currentState = STATE_NFC_READ;
        }
      }
      break;
    }
    
    // ----------------------------------------
    // NFC READ STATE - Waiting for button
    // ----------------------------------------
    case STATE_NFC_READ: {
      if (action == BTN_SHORT_PRESS) {
        // Start recording
        LOG_I("Main", "========================================");
        LOG_I("Main", "Recording audio (3 seconds)...");
        LOG_I("Main", "Speak into microphone!");
        LOG_I("Main", "========================================");
        
        if (!audio.configureForRecording()) {
          LOG_E("Main", "Failed to configure audio for recording");
          currentState = STATE_ERROR;
          break;
        }
        
        recordedSamples = 0;
        recordingStartTime = millis();
        currentState = STATE_RECORDING;
      }
      else if (action == BTN_LONG_PRESS) {
        // Play back recorded audio
        if (recordedSamples == 0) {
          LOG_W("Main", "No audio recorded yet!");
        } else {
          LOG_I("Main", "========================================");
          Logger::printf(LOG_INFO, "Main", "Playing back %d samples (%.1f seconds)...", 
                        recordedSamples, (float)recordedSamples / SAMPLE_RATE);
          LOG_I("Main", "========================================");
          
          if (!audio.configureForPlayback()) {
            LOG_E("Main", "Failed to configure audio for playback");
            currentState = STATE_ERROR;
            break;
          }
          
          playbackStartTime = millis();
          currentState = STATE_PLAYING;
        }
      }
      
      // Timeout after 10 seconds without button press
      static unsigned long nfcReadTime = 0;
      if (nfcReadTime == 0) {
        nfcReadTime = millis();
      }
      if (millis() - nfcReadTime > 10000) {
        LOG_I("Main", "NFC session timeout, returning to idle");
        nfcReadTime = 0;
        currentState = STATE_IDLE;
      }
      break;
    }
    
    // ----------------------------------------
    // RECORDING STATE
    // ----------------------------------------
    case STATE_RECORDING: {
      // Record for 3 seconds
      const uint32_t RECORD_DURATION_MS = 3000;
      
      if (millis() - recordingStartTime < RECORD_DURATION_MS) {
        // Read samples from microphone
        size_t samplesRead = audio.readSamples(audioBuffer + recordedSamples, 
                                               audioBufferSize - recordedSamples);
        recordedSamples += samplesRead;
        
        // Progress indicator every 500ms
        static unsigned long lastProgress = 0;
        if (millis() - lastProgress > 500) {
          lastProgress = millis();
          float elapsed = (float)(millis() - recordingStartTime) / 1000.0f;
          Logger::printf(LOG_INFO, "Main", "Recording... %.1fs / 3.0s", elapsed);
        }
      } else {
        // Recording complete
        LOG_I("Main", "========================================");
        Logger::printf(LOG_INFO, "Main", "Recording complete! Captured %d samples", recordedSamples);
        logHeapStatus();
        LOG_I("Main", "Long-press button to play back");
        LOG_I("Main", "========================================");
        currentState = STATE_NFC_READ;
      }
      break;
    }
    
    // ----------------------------------------
    // PLAYING STATE
    // ----------------------------------------
    case STATE_PLAYING: {
      // Calculate expected playback duration
      uint32_t playbackDuration = (recordedSamples * 1000) / SAMPLE_RATE;
      
      if (millis() - playbackStartTime < playbackDuration + 500) {  // +500ms buffer
        // Write samples to amplifier
        static size_t playbackIndex = 0;
        
        if (playbackIndex < recordedSamples) {
          size_t samplesToWrite = min((size_t)256, recordedSamples - playbackIndex);
          size_t samplesWritten = audio.writeSamples(audioBuffer + playbackIndex, samplesToWrite);
          playbackIndex += samplesWritten;
          
          // Progress indicator
          static unsigned long lastProgress = 0;
          if (millis() - lastProgress > 500) {
            lastProgress = millis();
            float progress = (float)playbackIndex / recordedSamples * 100.0f;
            Logger::printf(LOG_INFO, "Main", "Playback... %.0f%%", progress);
          }
        }
      } else {
        // Playback complete
        LOG_I("Main", "========================================");
        LOG_I("Main", "Playback complete!");
        LOG_I("Main", "Short-press to record again");
        LOG_I("Main", "========================================");
        currentState = STATE_NFC_READ;
      }
      break;
    }
    
    // ----------------------------------------
    // ERROR STATE
    // ----------------------------------------
    case STATE_ERROR: {
      static unsigned long lastErrorLog = 0;
      if (millis() - lastErrorLog > 2000) {
        lastErrorLog = millis();
        LOG_E("Main", "System in error state. Reset required.");
      }
      break;
    }
  }
  
  // Small delay to prevent tight loop
  delay(10);
}

// ============================================
// HELPER FUNCTIONS
// ============================================
void logHeapStatus() {
  Logger::printf(LOG_INFO, "Heap", "Free: %d bytes, Min free: %d bytes", 
                 ESP.getFreeHeap(), ESP.getMinFreeHeap());
}
