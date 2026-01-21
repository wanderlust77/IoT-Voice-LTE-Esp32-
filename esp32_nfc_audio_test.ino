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
AppState currentState = STATE_IDLE;
ErrorCode lastError = ERROR_NONE;

// ============================================
// AUDIO BUFFER (LOCAL STORAGE)
// ============================================
// Note: If allocation fails, reduce this value:
//   * 3 seconds = 96KB (recommended)
//   * 2 seconds = 64KB (if 3s fails)
//   * 1 second  = 32KB (minimal)
#define MAX_AUDIO_SAMPLES  (SAMPLE_RATE * 3)  // 3 seconds max at 16kHz (96KB)
int16_t* audioBuffer = nullptr;
size_t audioBufferSize = 0;
size_t recordedSamples = 0;

// ============================================
// AUDIO VOLUME CONTROL
// ============================================
// Software gain multiplier (1.0 = normal, 2.0 = 2x louder, etc.)
// MAX98357A with floating GAIN pin = 15dB hardware gain
// Increase this if audio is too quiet
#define AUDIO_GAIN_MULTIPLIER  3.0f  // 3x software gain (adjust as needed)

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
// FORWARD DECLARATIONS
// ============================================
void logHeapStatus();

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
  size_t bufferBytes = MAX_AUDIO_SAMPLES * sizeof(int16_t);
  size_t freeHeap = ESP.getFreeHeap();
  Logger::printf(LOG_INFO, "Main", "Free heap: %d bytes", freeHeap);
  Logger::printf(LOG_INFO, "Main", "Requesting: %d bytes (%d samples)", 
                 bufferBytes, MAX_AUDIO_SAMPLES);
  
  if (freeHeap < bufferBytes + 10000) {  // Leave 10KB safety margin
    Logger::printf(LOG_ERROR, "Main", "Insufficient heap! Need %d bytes, have %d bytes", bufferBytes, freeHeap);
    currentState = STATE_ERROR;
    return;
  }
  
  audioBuffer = (int16_t*)malloc(bufferBytes);
  if (!audioBuffer) {
    LOG_E("Main", "malloc() failed! Heap may be fragmented.");
    Logger::printf(LOG_ERROR, "Main", "Free heap: %d bytes, Min free: %d bytes", 
                   ESP.getFreeHeap(), ESP.getMinFreeHeap());
    currentState = STATE_ERROR;
    return;
  }
  
  audioBufferSize = MAX_AUDIO_SAMPLES;
  Logger::printf(LOG_INFO, "Main", "Audio buffer allocated: %d samples (%.1f seconds, %d bytes)", 
                 audioBufferSize, (float)audioBufferSize / SAMPLE_RATE, bufferBytes);
  Logger::printf(LOG_INFO, "Main", "Remaining heap: %d bytes", ESP.getFreeHeap());
  
  // Initialize button
  LOG_I("Main", "Initializing button...");
  button.init(PIN_BUTTON, 2000, 50);  // 2s long press, 50ms debounce
  LOG_I("Main", "NOTE: GPIO34 requires EXTERNAL pull-up resistor!");
  Serial.flush();  // Ensure message is sent
  
  // Initialize NFC
  LOG_I("Main", "Initializing NFC...");
  Serial.flush();
  unsigned long nfcStartTime = millis();
  bool nfcSuccess = nfc.init(PIN_NFC_SDA, PIN_NFC_SCL, PIN_NFC_IRQ, PIN_NFC_RST);
  unsigned long nfcDuration = millis() - nfcStartTime;
  Logger::printf(LOG_INFO, "Main", "NFC init took %lu ms", nfcDuration);
  
  if (!nfcSuccess) {
    LOG_E("Main", "NFC init failed!");
    LOG_E("Main", "Check: 1) I2C wiring, 2) PN532 power, 3) I2C address");
    LOG_E("Main", "You can continue without NFC for audio testing");
    // Don't return - allow audio testing even if NFC fails
    // currentState = STATE_ERROR;
    // return;
  } else {
    LOG_I("Main", "NFC initialized successfully");
  }
  Serial.flush();
  
  // Initialize Audio
  LOG_I("Main", "Initializing audio...");
  Serial.flush();
  unsigned long audioStartTime = millis();
  bool audioSuccess = audio.init(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_MIC_DATA, PIN_I2S_AMP_DATA);
  unsigned long audioDuration = millis() - audioStartTime;
  Logger::printf(LOG_INFO, "Main", "Audio init took %lu ms", audioDuration);
  
  if (!audioSuccess) {
    LOG_E("Main", "Audio init failed!");
    currentState = STATE_ERROR;
    return;
  } else {
    LOG_I("Main", "Audio initialized successfully");
  }
  Serial.flush();
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "Initialization complete!");
  LOG_I("Main", "");
  LOG_I("Main", "Usage:");
  LOG_I("Main", "1. Present NFC tag to reader");
  LOG_I("Main", "2. SHORT press button → Record 3 seconds");
  LOG_I("Main", "3. LONG press button → Playback recording");
  LOG_I("Main", "========================================");
  
  logHeapStatus();
  
  // Start in IDLE state, waiting for NFC tag
  currentState = STATE_IDLE;
  recordedSamples = 0;
}

// ============================================
// MAIN LOOP
// ============================================
// Helper function to format UID
void formatUIDString(const uint8_t* uid, uint8_t length, char* buffer) {
  for (uint8_t i = 0; i < length; i++) {
    sprintf(buffer + (i * 3), "%02X", uid[i]);
    if (i < length - 1) {
      buffer[i * 3 + 2] = ':';
    }
  }
  buffer[length * 3 - 1] = '\0';
}

void loop() {
  // Update button state
  button.update();
  
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
          formatUIDString(nfcUID, nfcUIDLength, uidStr);
          LOG_I("Main", "========================================");
          Logger::printf(LOG_INFO, "Main", "NFC Tag Detected: %s", uidStr);
          LOG_I("Main", "Press button to record or playback");
          LOG_I("Main", "========================================");
          currentState = STATE_READING_NFC;
        }
      }
      break;
    }
    
    // ----------------------------------------
    // NFC READ STATE - Waiting for button
    // ----------------------------------------
    case STATE_READING_NFC: {
      if (button.wasShortPress()) {
        // Start recording
        LOG_I("Main", "========================================");
        LOG_I("Main", "Recording audio (3 seconds)...");
        LOG_I("Main", "Speak into microphone!");
        LOG_I("Main", "========================================");
        
        if (!audio.startRecording(SAMPLE_RATE)) {
          LOG_E("Main", "Failed to start audio recording");
          currentState = STATE_ERROR;
          break;
        }
        
        recordedSamples = 0;
        recordingStartTime = millis();
        currentState = STATE_RECORDING;
      }
      else if (button.wasLongPress()) {
        // Play back recorded audio
        if (recordedSamples == 0) {
          LOG_W("Main", "No audio recorded yet!");
        } else {
          LOG_I("Main", "========================================");
          size_t sampleBytes = recordedSamples * sizeof(int16_t);
          Logger::printf(LOG_INFO, "Main", "Playing back %d samples (%d bytes, %.1f seconds)...", 
                        recordedSamples, sampleBytes, (float)recordedSamples / SAMPLE_RATE);
          LOG_I("Main", "========================================");
          
          if (!audio.startPlayback(SAMPLE_RATE)) {
            LOG_E("Main", "Failed to start audio playback");
            currentState = STATE_ERROR;
            break;
          }
          
          playbackStartTime = millis();
          currentState = STATE_PLAYING;
        }
      }
      
      // Timeout after 10 seconds without button press
      {
        static unsigned long nfcReadTime = 0;
        if (nfcReadTime == 0) {
          nfcReadTime = millis();
        }
        if (millis() - nfcReadTime > 10000) {
          LOG_I("Main", "NFC session timeout, returning to idle");
          nfcReadTime = 0;
          currentState = STATE_IDLE;
        }
      }
      break;
    }
    
    // ----------------------------------------
    // RECORDING STATE
    // ----------------------------------------
    case STATE_RECORDING: {
      // Read audio data in chunks
      static int16_t readBuffer[256];  // Temporary buffer for reading
      size_t bytesAvailable = sizeof(readBuffer);
      uint8_t* bufferPtr = (uint8_t*)readBuffer;
      size_t bytesRead = audio.readRecordedData(bufferPtr, bytesAvailable);
      
      if (bytesRead > 0) {
        size_t samplesRead = bytesRead / sizeof(int16_t);
        
        // Copy samples to main buffer
        if (recordedSamples + samplesRead <= audioBufferSize) {
          memcpy(&audioBuffer[recordedSamples], readBuffer, bytesRead);
          recordedSamples += samplesRead;
        } else {
          // Buffer full - stop recording
          LOG_W("Main", "Audio buffer full! Stopping recording.");
          audio.stopRecording();
          currentState = STATE_READING_NFC;
          break;
        }
        
        // Progress indicator
        unsigned long elapsed = millis() - recordingStartTime;
        float progress = (float)elapsed / 3000.0f * 100.0f;  // 3 seconds = 100%
        static unsigned long lastProgress = 0;
        if (millis() - lastProgress > 500) {
          lastProgress = millis();
          Logger::printf(LOG_INFO, "Main", "Recording... %.1fs / 3.0s (samples: %d)", 
                        elapsed / 1000.0f, recordedSamples);
        }
      }
      
      // Check if 3 seconds have elapsed
      if (millis() - recordingStartTime >= 3000) {
        // Recording complete
        audio.stopRecording();
        LOG_I("Main", "========================================");
        Logger::printf(LOG_INFO, "Main", "Recording complete! Captured %d samples", recordedSamples);
        Logger::printf(LOG_INFO, "Main", "Long-press button to play back (gain: %.1fx)", AUDIO_GAIN_MULTIPLIER);
        LOG_I("Main", "========================================");
        currentState = STATE_READING_NFC;
      }
      break;
    }
    
    // ----------------------------------------
    // PLAYING STATE
    // ----------------------------------------
    case STATE_PLAYING: {
      // Write all audio data
      {
        static size_t playbackIndex = 0;
        static bool playbackStarted = false;
        
        if (!playbackStarted) {
          playbackIndex = 0;
          playbackStarted = true;
        }
        
        if (playbackIndex < recordedSamples) {
          // Apply software gain and prepare audio chunk
          size_t samplesToProcess = min((size_t)256, recordedSamples - playbackIndex);
          static int16_t gainBuffer[256];  // Temporary buffer for gain-adjusted samples
          
          // Apply gain multiplier to samples
          for (size_t i = 0; i < samplesToProcess; i++) {
            int32_t sample = (int32_t)audioBuffer[playbackIndex + i];
            sample = (int32_t)(sample * AUDIO_GAIN_MULTIPLIER);
            
            // Clamp to 16-bit range to prevent clipping
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            
            gainBuffer[i] = (int16_t)sample;
          }
          
          // Write gain-adjusted samples
          size_t bytesToWrite = samplesToProcess * sizeof(int16_t);
          size_t bytesWritten = audio.writePlaybackData((uint8_t*)gainBuffer, bytesToWrite);
          playbackIndex += bytesWritten / sizeof(int16_t);
          
          // Progress indicator
          static unsigned long lastProgress = 0;
          if (millis() - lastProgress > 500) {
            lastProgress = millis();
            float progress = (float)playbackIndex / recordedSamples * 100.0f;
            Logger::printf(LOG_INFO, "Main", "Playback... %.0f%%", progress);
          }
        } else {
          // Playback complete
          audio.stopPlayback();
          playbackStarted = false;
          LOG_I("Main", "========================================");
          LOG_I("Main", "Playback complete!");
          LOG_I("Main", "Did you feel/hear the audio? If not, try:");
          Logger::printf(LOG_INFO, "Main", "  1. Increase AUDIO_GAIN_MULTIPLIER (currently %.1fx)", AUDIO_GAIN_MULTIPLIER);
          LOG_I("Main", "  2. Check speaker wiring and connections");
          LOG_I("Main", "  3. Verify MAX98357A SD pin is HIGH (3.3V)");
          LOG_I("Main", "Short-press to record again");
          LOG_I("Main", "========================================");
          currentState = STATE_READING_NFC;
        }
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
