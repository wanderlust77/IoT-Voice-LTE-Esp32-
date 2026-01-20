/*
 * esp32_voice_lte.ino
 * 
 * Main firmware for ESP32-based NFC-triggered voice recording/playback device
 * 
 * Hardware:
 * - ESP32 DevKitC-32E
 * - LTE Cat-M modem (MIKROE-6287)
 * - NFC reader (PN532)
 * - I2S microphone (SPH0645LM4H)
 * - I2S amplifier (MAX98357A)
 * - Button for user input
 */

#include "hardware_defs.h"
#include "config.h"
#include "app_state.h"
#include "logger.h"
#include "button_handler.h"
#include "nfc_manager.h"
#include "audio_manager.h"
#include "lte_manager.h"

// ============================================
// GLOBAL OBJECTS
// ============================================
ButtonHandler button;
NFCManager nfc;
AudioManager audio;
LTEManager lte;

// ============================================
// STATE MACHINE VARIABLES
// ============================================
AppState currentState = STATE_INIT;
AppState previousState = STATE_INIT;
ActionType currentAction = ACTION_NONE;
ErrorCode lastError = ERROR_NONE;

// NFC data
uint8_t nfcUID[10];
uint8_t nfcUIDLength = 0;
char nfcUIDString[32];

// Timing
unsigned long stateStartTime = 0;
unsigned long nfcReadTimeout = 0;

// Audio buffers
uint8_t* audioBuffer = NULL;
size_t audioBufferSize = AUDIO_BUFFER_SIZE;
size_t audioDataLength = 0;

// Recording state
unsigned long recordingStartTime = 0;
size_t recordingLength = 0;

// Retry counters
int retryCount = 0;

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize logger first
  Logger::init(SERIAL_BAUD_RATE);
  LOG_I("Main", "===================================");
  LOG_I("Main", "ESP32 Voice LTE - Starting up");
  LOG_I("Main", "===================================");
  
  // Log free heap
  logHeapStatus();
  
  // Allocate audio buffer
  audioBuffer = (uint8_t*)malloc(audioBufferSize);
  if (!audioBuffer) {
    LOG_E("Main", "Failed to allocate audio buffer!");
    currentState = STATE_ERROR;
    lastError = ERROR_OUT_OF_MEMORY;
    return;
  }
  LOG_I("Main", "Audio buffer allocated");
  
  // Initialize button handler
  LOG_I("Main", "Initializing button...");
  button.init(PIN_BUTTON, LONG_PRESS_MS, DEBOUNCE_MS);
  
  // Initialize NFC reader
  LOG_I("Main", "Initializing NFC...");
  if (!nfc.init(PIN_NFC_SDA, PIN_NFC_SCL, PIN_NFC_IRQ, PIN_NFC_RST)) {
    LOG_E("Main", "NFC initialization failed!");
    currentState = STATE_ERROR;
    lastError = ERROR_NFC_INIT;
    return;
  }
  
  // Initialize audio manager
  LOG_I("Main", "Initializing audio...");
  if (!audio.init(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_MIC_DATA, PIN_I2S_AMP_DATA)) {
    LOG_E("Main", "Audio initialization failed!");
    currentState = STATE_ERROR;
    lastError = ERROR_AUDIO_INIT;
    return;
  }
  
  // Initialize LTE modem
  LOG_I("Main", "Initializing LTE...");
  if (!lte.init(PIN_LTE_TX, PIN_LTE_RX, PIN_LTE_PWRKEY, PIN_LTE_RESET, LTE_BAUD_RATE)) {
    LOG_E("Main", "LTE initialization failed!");
    currentState = STATE_ERROR;
    lastError = ERROR_LTE_INIT;
    return;
  }
  
  // Power on LTE modem
  LOG_I("Main", "Powering on LTE modem...");
  if (!lte.powerOn()) {
    LOG_E("Main", "LTE power on failed!");
    currentState = STATE_ERROR;
    lastError = ERROR_LTE_INIT;
    return;
  }
  
  // Check network registration (with timeout)
  LOG_I("Main", "Checking network...");
  if (!lte.checkNetwork(30000)) {
    LOG_W("Main", "Network not registered (will retry on demand)");
    // Don't fail - we can retry later
  }
  
  // Configure bearer
  if (!lte.configureBearerAPN(LTE_APN)) {
    LOG_W("Main", "Failed to configure APN (will retry on demand)");
  }
  
  // Open bearer
  if (!lte.openBearer()) {
    LOG_W("Main", "Failed to open bearer (will retry on demand)");
  }
  
  // Initialization complete
  LOG_I("Main", "===================================");
  LOG_I("Main", "Initialization complete!");
  LOG_I("Main", "Ready for operation");
  LOG_I("Main", "===================================");
  logHeapStatus();
  
  transitionTo(STATE_IDLE);
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();
  
  // Update subsystems (non-blocking)
  button.update();
  lte.update();
  
  // State machine
  switch (currentState) {
    
    // ========================================
    // IDLE STATE
    // ========================================
    case STATE_IDLE:
      // Wait for button press
      if (button.wasShortPress()) {
        LOG_I("Main", "Short press detected -> PLAYBACK");
        currentAction = ACTION_PLAYBACK;
        transitionTo(STATE_READING_NFC);
      } 
      else if (button.wasLongPress()) {
        LOG_I("Main", "Long press detected -> RECORD");
        currentAction = ACTION_RECORD;
        transitionTo(STATE_READING_NFC);
      }
      break;
    
    // ========================================
    // READING NFC STATE
    // ========================================
    case STATE_READING_NFC:
      // Set timeout on first entry
      if (stateStartTime == now) {
        nfcReadTimeout = now + NFC_READ_TIMEOUT_MS;
        LOG_I("Main", "Reading NFC UID...");
      }
      
      // Try to read NFC UID
      if (nfc.readUID(nfcUID, &nfcUIDLength, 0)) {
        // UID read successfully
        formatNfcUID();
        Logger::printf(LOG_INFO, "Main", "NFC UID: %s", nfcUIDString);
        
        // Transition based on action type
        if (currentAction == ACTION_PLAYBACK) {
          transitionTo(STATE_FETCH_AUDIO);
        } else if (currentAction == ACTION_RECORD) {
          transitionTo(STATE_RECORDING);
        }
      }
      else if (now > nfcReadTimeout) {
        // Timeout
        LOG_E("Main", "NFC read timeout");
        lastError = ERROR_NFC_READ;
        transitionTo(STATE_IDLE);
      }
      break;
    
    // ========================================
    // FETCH AUDIO STATE
    // ========================================
    case STATE_FETCH_AUDIO: {
      // Enter state
      if (stateStartTime == now) {
        LOG_I("Main", "Fetching audio from server...");
        
        // Build URL with UID
        char url[256];
        snprintf(url, sizeof(url), "%s/audio?uid=%s", API_ENDPOINT, nfcUIDString);
        Logger::printf(LOG_INFO, "Main", "URL: %s", url);
        
        // Perform HTTP GET
        if (lte.httpGet(url, audioBuffer, &audioDataLength, audioBufferSize)) {
          Logger::printf(LOG_INFO, "Main", "Audio fetched: %d bytes", audioDataLength);
          
          if (audioDataLength > 0) {
            transitionTo(STATE_PLAYING);
          } else {
            LOG_W("Main", "No audio data received");
            transitionTo(STATE_IDLE);
          }
        } else {
          LOG_E("Main", "HTTP GET failed");
          lastError = ERROR_HTTP_GET;
          
          // Retry logic
          retryCount++;
          if (retryCount < HTTP_RETRY_COUNT) {
            Logger::printf(LOG_WARN, "Main", "Retrying... (%d/%d)", retryCount, HTTP_RETRY_COUNT);
            // Stay in same state, will retry next loop
            stateStartTime = now;  // Reset state timer
          } else {
            LOG_E("Main", "Max retries reached");
            retryCount = 0;
            transitionTo(STATE_IDLE);
          }
        }
      }
      break;
    }
    
    // ========================================
    // PLAYING STATE
    // ========================================
    case STATE_PLAYING:
      // Enter state
      if (stateStartTime == now) {
        LOG_I("Main", "Playing audio...");
        
        // Start playback
        if (!audio.startPlayback(SAMPLE_RATE)) {
          LOG_E("Main", "Failed to start playback");
          lastError = ERROR_AUDIO_PLAYBACK;
          transitionTo(STATE_IDLE);
          break;
        }
        
        // Write audio data
        size_t written = audio.writePlaybackData(audioBuffer, audioDataLength);
        Logger::printf(LOG_INFO, "Main", "Wrote %d bytes to audio", written);
        
        // Stop playback
        audio.stopPlayback();
        
        LOG_I("Main", "Playback complete");
        transitionTo(STATE_IDLE);
      }
      break;
    
    // ========================================
    // RECORDING STATE
    // ========================================
    case STATE_RECORDING: {
      // Enter state
      if (stateStartTime == now) {
        LOG_I("Main", "Starting recording...");
        recordingStartTime = now;
        recordingLength = 0;
        
        // Start recording
        if (!audio.startRecording(SAMPLE_RATE)) {
          LOG_E("Main", "Failed to start recording");
          lastError = ERROR_AUDIO_RECORD;
          transitionTo(STATE_IDLE);
          break;
        }
        
        LOG_I("Main", "Recording... (release button to stop)");
      }
      
      // Record audio data
      if (recordingLength < audioBufferSize) {
        // Read chunk of audio
        uint8_t tempBuf[512];
        size_t bytesRead = audio.readRecordedData(tempBuf, sizeof(tempBuf));
        
        if (bytesRead > 0) {
          // Convert 32-bit samples to 16-bit (SPH0645 outputs 32-bit, we want 16-bit)
          size_t samples = bytesRead / 4;
          for (size_t i = 0; i < samples && recordingLength < audioBufferSize; i++) {
            // Take upper 16 bits of 32-bit sample
            int32_t sample32 = *((int32_t*)(tempBuf + i * 4));
            int16_t sample16 = (int16_t)(sample32 >> 16);
            
            // Store in buffer
            *((int16_t*)(audioBuffer + recordingLength)) = sample16;
            recordingLength += 2;
          }
        }
      }
      
      // Check stop conditions
      unsigned long recordingDuration = now - recordingStartTime;
      
      if (!button.isCurrentlyPressed()) {
        // Button released - stop recording
        audio.stopRecording();
        Logger::printf(LOG_INFO, "Main", "Recording stopped: %d bytes, %lu ms", 
                       recordingLength, recordingDuration);
        transitionTo(STATE_UPLOADING);
      }
      else if (recordingDuration >= MAX_RECORDING_MS) {
        // Max duration reached
        audio.stopRecording();
        LOG_W("Main", "Max recording duration reached");
        transitionTo(STATE_UPLOADING);
      }
      else if (recordingLength >= audioBufferSize) {
        // Buffer full
        audio.stopRecording();
        LOG_W("Main", "Recording buffer full");
        transitionTo(STATE_UPLOADING);
      }
      break;
    }
    
    // ========================================
    // UPLOADING STATE
    // ========================================
    case STATE_UPLOADING: {
      // Enter state
      if (stateStartTime == now) {
        LOG_I("Main", "Uploading audio to server...");
        
        // Build URL with UID
        char url[256];
        snprintf(url, sizeof(url), "%s/upload?uid=%s", API_ENDPOINT, nfcUIDString);
        Logger::printf(LOG_INFO, "Main", "URL: %s", url);
        
        // Perform HTTP POST
        if (lte.httpPost(url, audioBuffer, recordingLength)) {
          LOG_I("Main", "Upload successful");
          transitionTo(STATE_IDLE);
        } else {
          LOG_E("Main", "HTTP POST failed");
          lastError = ERROR_HTTP_POST;
          
          // Retry logic
          retryCount++;
          if (retryCount < HTTP_RETRY_COUNT) {
            Logger::printf(LOG_WARN, "Main", "Retrying... (%d/%d)", retryCount, HTTP_RETRY_COUNT);
            stateStartTime = now;  // Reset state timer
          } else {
            LOG_E("Main", "Max retries reached");
            retryCount = 0;
            transitionTo(STATE_IDLE);
          }
        }
      }
      break;
    }
    
    // ========================================
    // ERROR STATE
    // ========================================
    case STATE_ERROR:
      // Log error and halt
      Logger::printf(LOG_ERROR, "Main", "FATAL ERROR: %d", lastError);
      LOG_E("Main", "System halted. Reset required.");
      delay(1000);
      // Stay in error state
      break;
    
    // ========================================
    // INIT STATE (should not be here)
    // ========================================
    case STATE_INIT:
    default:
      // Should not reach here during normal operation
      break;
  }
  
  // Small delay to prevent tight loop
  delay(10);
}

// ============================================
// STATE TRANSITION
// ============================================
void transitionTo(AppState newState) {
  if (newState != currentState) {
    Logger::printf(LOG_INFO, "Main", "State: %s -> %s", 
                   getStateName(currentState), getStateName(newState));
    
    previousState = currentState;
    currentState = newState;
    stateStartTime = millis();
    
    // Reset retry counter on successful transition
    if (newState == STATE_IDLE) {
      retryCount = 0;
      currentAction = ACTION_NONE;
    }
    
    // Log heap status on state transitions
    if (currentState == STATE_IDLE) {
      logHeapStatus();
    }
  }
}

// ============================================
// GET STATE NAME (for logging)
// ============================================
const char* getStateName(AppState state) {
  switch (state) {
    case STATE_INIT:         return "INIT";
    case STATE_IDLE:         return "IDLE";
    case STATE_READING_NFC:  return "READING_NFC";
    case STATE_FETCH_AUDIO:  return "FETCH_AUDIO";
    case STATE_PLAYING:      return "PLAYING";
    case STATE_RECORDING:    return "RECORDING";
    case STATE_UPLOADING:    return "UPLOADING";
    case STATE_ERROR:        return "ERROR";
    default:                 return "UNKNOWN";
  }
}

// ============================================
// FORMAT NFC UID AS HEX STRING
// ============================================
void formatNfcUID() {
  size_t pos = 0;
  for (uint8_t i = 0; i < nfcUIDLength && pos < sizeof(nfcUIDString) - 3; i++) {
    snprintf(nfcUIDString + pos, sizeof(nfcUIDString) - pos, "%02X", nfcUID[i]);
    pos += 2;
  }
  nfcUIDString[pos] = '\0';
}

// ============================================
// LOG HEAP STATUS
// ============================================
void logHeapStatus() {
  uint32_t freeHeap = ESP.getFreeHeap();
  Logger::printf(LOG_INFO, "Main", "Free heap: %u bytes", freeHeap);
  
  if (freeHeap < MIN_FREE_HEAP) {
    LOG_W("Main", "Low memory warning!");
  }
}
