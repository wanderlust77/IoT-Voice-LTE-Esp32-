/*
 * audio_manager.cpp
 * 
 * Implementation of I2S audio manager
 */

#include "audio_manager.h"
#include "logger.h"
#include "config.h"

// I2S port numbers - use separate ports for mic and amp
#define I2S_PORT_RECORDING I2S_NUM_0  // Microphone (RX)
#define I2S_PORT_PLAYBACK  I2S_NUM_1  // Amplifier (TX)

// ============================================
// INITIALIZE AUDIO MANAGER
// ============================================
bool AudioManager::init(uint8_t micBclkPin, uint8_t micLrclkPin, uint8_t micDataPin, 
                        uint8_t ampBclkPin, uint8_t ampLrclkPin, uint8_t ampDataPin) {
  pinMicBclk = micBclkPin;
  pinMicLrclk = micLrclkPin;
  pinMicData = micDataPin;
  pinAmpBclk = ampBclkPin;
  pinAmpLrclk = ampLrclkPin;
  pinAmpData = ampDataPin;
  
  currentMode = AUDIO_MODE_NONE;
  initialized = true;
  currentSampleRate = SAMPLE_RATE;
  
  Logger::printf(LOG_INFO, "Audio", "Audio manager initialized");
  Logger::printf(LOG_INFO, "Audio", "Mic pins: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d", 
                 pinMicBclk, pinMicLrclk, pinMicData);
  Logger::printf(LOG_INFO, "Audio", "Amp pins: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d", 
                 pinAmpBclk, pinAmpLrclk, pinAmpData);
  return true;
}

// ============================================
// START PLAYBACK MODE
// ============================================
bool AudioManager::startPlayback(uint32_t sampleRate) {
  if (!initialized) {
    LOG_E("Audio", "Not initialized");
    return false;
  }
  
  LOG_I("Audio", "Starting playback mode...");
  
  if (!reconfigureI2S(AUDIO_MODE_PLAYBACK, sampleRate)) {
    LOG_E("Audio", "Failed to configure I2S for playback");
    return false;
  }
  
  LOG_I("Audio", "Playback mode ready");
  return true;
}

// ============================================
// WRITE PLAYBACK DATA
// ============================================
size_t AudioManager::writePlaybackData(const uint8_t* data, size_t length) {
  if (currentMode != AUDIO_MODE_PLAYBACK) {
    LOG_E("Audio", "Not in playback mode");
    return 0;
  }
  
  size_t bytesWritten = 0;
  esp_err_t result = i2s_write(I2S_PORT_PLAYBACK, data, length, &bytesWritten, portMAX_DELAY);
  
  if (result != ESP_OK) {
    Logger::printf(LOG_ERROR, "Audio", "I2S write failed: %d", result);
    return 0;
  }
  
  return bytesWritten;
}

// ============================================
// STOP PLAYBACK
// ============================================
void AudioManager::stopPlayback() {
  if (currentMode == AUDIO_MODE_PLAYBACK) {
    LOG_I("Audio", "Stopping playback");
    
    // Drain any remaining data
    i2s_zero_dma_buffer(I2S_PORT_PLAYBACK);
    
    shutdownI2S();
  }
}

// ============================================
// START RECORDING MODE
// ============================================
bool AudioManager::startRecording(uint32_t sampleRate) {
  if (!initialized) {
    LOG_E("Audio", "Not initialized");
    return false;
  }
  
  LOG_I("Audio", "Starting recording mode...");
  
  if (!reconfigureI2S(AUDIO_MODE_RECORDING, sampleRate)) {
    LOG_E("Audio", "Failed to configure I2S for recording");
    return false;
  }
  
  // Clear DMA buffer to avoid reading stale data
  i2s_zero_dma_buffer(I2S_PORT_RECORDING);
  
  // ESP32 I2S RX mode: Sometimes LRCLK doesn't start until we start reading
  // Trigger a dummy read to start the clocks properly
  uint8_t dummyBuffer[64];
  size_t bytesRead = 0;
  i2s_read(I2S_PORT_RECORDING, dummyBuffer, sizeof(dummyBuffer), &bytesRead, 0);  // Non-blocking
  
  // Wait for microphone to stabilize and clocks to start
  delay(500);
  
  LOG_I("Audio", "Recording mode ready");
  Logger::printf(LOG_INFO, "Audio", "I2S configured: %lu Hz, 32-bit, RX mode (I2S_NUM_0)", sampleRate);
  Logger::printf(LOG_INFO, "Audio", "Pins: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d", 
                 pinMicBclk, pinMicLrclk, pinMicData);
  Logger::printf(LOG_INFO, "Audio", "Format: STAND_I2S, Channel: LEFT only");
  LOG_I("Audio", "NOTE: Triggered dummy read to start LRCLK");
  return true;
}

// ============================================
// READ RECORDED DATA
// ============================================
// SPH0645LM4H outputs 32-bit samples with 18-bit audio data (left-aligned)
// We need to extract the 18-bit data and convert to 16-bit
size_t AudioManager::readRecordedData(uint8_t* buffer, size_t maxLength) {
  if (currentMode != AUDIO_MODE_RECORDING) {
    LOG_E("Audio", "Not in recording mode");
    return 0;
  }
  
  // Read 32-bit samples from I2S
  // Allocate temporary buffer for 32-bit samples
  static uint32_t i2sBuffer[256];  // Max 256 samples at a time
  size_t samplesToRead = (maxLength / sizeof(int16_t)) < 256 ? (maxLength / sizeof(int16_t)) : 256;
  size_t bytesToRead = samplesToRead * sizeof(uint32_t);  // 32-bit samples from I2S
  
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_PORT_RECORDING, i2sBuffer, bytesToRead, &bytesRead, 0);  // Non-blocking
  
  static int readCallCount = 0;
  readCallCount++;
  
  // Log first few reads with details
  if (readCallCount <= 5) {
    Logger::printf(LOG_INFO, "Audio", "I2S read #%d: result=%d, bytesRead=%d, bytesToRead=%d", 
                   readCallCount, result, bytesRead, bytesToRead);
    if (bytesRead > 0) {
      Logger::printf(LOG_INFO, "Audio", "Raw 32-bit (hex): %08X, %08X, %08X, %08X", 
                     i2sBuffer[0], i2sBuffer[1], i2sBuffer[2], i2sBuffer[3]);
    }
  }
  
  if (result != ESP_OK) {
    static unsigned long lastError = 0;
    if (millis() - lastError > 2000) {  // Log errors every 2 seconds
      lastError = millis();
      Logger::printf(LOG_ERROR, "Audio", "I2S read failed: %d", result);
    }
    return 0;
  }
  
  if (bytesRead == 0) {
    // This is normal if no data is available yet (non-blocking mode)
    // But log it occasionally so we know what's happening
    static int zeroReadCount = 0;
    zeroReadCount++;
    if (zeroReadCount == 1 || zeroReadCount % 100 == 0) {
      Logger::printf(LOG_INFO, "Audio", "I2S read returned 0 bytes (call #%d) - this is normal if buffer empty", zeroReadCount);
    }
    return 0;
  }
  
  // Convert 32-bit samples to 16-bit
  // SPH0645LM4H outputs 32-bit words with 18-bit audio data
  // The format is: bits 0-17 contain 18-bit left-aligned audio data
  // We need to extract and convert to 16-bit
  size_t samplesRead = bytesRead / sizeof(uint32_t);
  int16_t* outputBuffer = (int16_t*)buffer;
  
  // Log raw 32-bit I2S values - ALWAYS log first 10 reads, then periodically
  // Also check if raw values are actually varying (even slightly)
  static int dataReadCount = 0;
  static uint32_t lastRawValues[4] = {0, 0, 0, 0};
  static bool rawValuesVarying = false;
  dataReadCount++;
  
  // Check if raw values are varying
  if (dataReadCount <= 2 && samplesRead >= 4) {
    // Compare with previous values
    bool varying = false;
    for (int i = 0; i < 4 && i < (int)samplesRead; i++) {
      if (i2sBuffer[i] != lastRawValues[i]) {
        varying = true;
        rawValuesVarying = true;
        break;
      }
      lastRawValues[i] = i2sBuffer[i];
    }
    if (varying && dataReadCount == 2) {
      LOG_I("Audio", "✅ Raw I2S values ARE varying - microphone is sending data!");
    }
  }
  
  if (dataReadCount <= 10 || dataReadCount % 50 == 0) {
    Logger::printf(LOG_INFO, "Audio", "Raw I2S #%d: samples=%d, raw[0]=0x%08X, raw[1]=0x%08X, raw[2]=0x%08X, raw[3]=0x%08X", 
                   dataReadCount, samplesRead, 
                   samplesRead > 0 ? i2sBuffer[0] : 0,
                   samplesRead > 1 ? i2sBuffer[1] : 0,
                   samplesRead > 2 ? i2sBuffer[2] : 0,
                   samplesRead > 3 ? i2sBuffer[3] : 0);
    
    // If raw values are constant 0x00000001, suggest checking I2S format
    if (dataReadCount <= 3 && samplesRead >= 1) {
      if (i2sBuffer[0] == 0x00000001 && i2sBuffer[1] == 0x00000001) {
        LOG_W("Audio", "⚠️  Raw values are constant 0x00000001");
        LOG_W("Audio", "If hardware is OK, possible causes:");
        LOG_W("Audio", "1. I2S format/alignment wrong (try STAND_MSB or I2S_MSB)");
        LOG_W("Audio", "2. Clock timing issue (BCLK/LRCLK alignment)");
        LOG_W("Audio", "3. Microphone very quiet (try speaking loudly)");
        LOG_W("Audio", "4. I2S data line not connected correctly");
      }
    }
  }
  
  // SPH0645LM4H sends stereo frames (LEFT, RIGHT, LEFT, RIGHT, ...)
  // SEL = GND means LEFT channel is active
  // Extract 24-bit signed PCM, MSB-aligned in 32-bit word: shift right by 8
  // Process only LEFT channel samples (even indices: 0, 2, 4, ...)
  
  size_t monoSampleCount = 0;
  for (size_t i = 0; i < samplesRead; i += 2) {  // Step by 2 to get LEFT channel only
    if (i >= samplesRead) break;  // Safety check
    
    uint32_t raw = i2sBuffer[i];  // LEFT channel (even index)
    // Extract 24-bit signed PCM, MSB-aligned: shift right by 8
    int32_t rawSigned = (int32_t)raw;
    int16_t pcm = (int16_t)(rawSigned >> 8);
    
    outputBuffer[monoSampleCount++] = pcm;
  }
  
  // Track intermittent zero-data periods for automatic recovery
  static int consecutiveZeroReads = 0;
  static int totalReads = 0;
  static int zeroReads = 0;
  static unsigned long lastNonZeroTime = 0;
  
  totalReads++;
  
  // Check if ALL samples in this read are zero
  bool allZerosThisRead = true;
  for (size_t i = 0; i < samplesRead && i < 10; i++) {
    if (i2sBuffer[i] != 0x00000000) {
      allZerosThisRead = false;
      lastNonZeroTime = millis();
      break;
    }
  }
  
  if (allZerosThisRead) {
    consecutiveZeroReads++;
    zeroReads++;
    
    // Log warning if we get many consecutive zeros (intermittent issue)
    if (consecutiveZeroReads == 10) {
      LOG_W("Audio", "⚠️  Intermittent issue: 10 consecutive zero reads detected");
      LOG_W("Audio", "Microphone was working but now sending zeros.");
    } else if (consecutiveZeroReads == 50) {
      LOG_W("Audio", "⚠️  INTERMITTENT FAILURE: 50 consecutive zero reads!");
      LOG_W("Audio", "Microphone data stopped. Check: power, wiring, loose connections");
      LOG_W("Audio", "Attempting automatic I2S restart...");
      
      // Automatic recovery: restart I2S
      if (currentMode == AUDIO_MODE_RECORDING) {
        uint32_t savedRate = currentSampleRate;
        shutdownI2S();
        delay(100);
        if (reconfigureI2S(AUDIO_MODE_RECORDING, savedRate)) {
          LOG_I("Audio", "✅ I2S restarted successfully - monitoring for recovery");
          consecutiveZeroReads = 0;  // Reset counter
        } else {
          LOG_E("Audio", "❌ I2S restart failed!");
        }
      }
    }
  } else {
    // Got non-zero data - reset counter
    if (consecutiveZeroReads > 0) {
      Logger::printf(LOG_INFO, "Audio", "✅ Recovered: Got non-zero data after %d zero reads", consecutiveZeroReads);
      consecutiveZeroReads = 0;
    }
  }
  
  // Log statistics periodically
  static unsigned long lastStatsLog = 0;
  if (millis() - lastStatsLog > 10000) {  // Every 10 seconds
    lastStatsLog = millis();
    float zeroPercent = (totalReads > 0) ? (100.0f * zeroReads / totalReads) : 0.0f;
    unsigned long timeSinceNonZero = (lastNonZeroTime > 0) ? (millis() - lastNonZeroTime) : 0;
    Logger::printf(LOG_INFO, "Audio", "Stats: %d reads, %.1f%% zeros, %lu ms since last non-zero", 
                   totalReads, zeroPercent, timeSinceNonZero);
  }
  
  // Log first-time all-zero warning (one-time)
  static bool loggedAllZeros = false;
  if (!loggedAllZeros && allZerosThisRead && totalReads == 1) {
    loggedAllZeros = true;
    LOG_W("Audio", "========================================");
    LOG_W("Audio", "WARNING: All I2S samples are 0x00000000!");
    LOG_W("Audio", "I2S clocks are working, but microphone sends no data.");
    LOG_W("Audio", "");
    LOG_W("Audio", "TROUBLESHOOTING:");
    LOG_W("Audio", "1. Measure VDD pin on microphone (should be 3.3V)");
    LOG_W("Audio", "2. Verify SEL pin is connected to GND (confirmed ✅)");
    LOG_W("Audio", "3. Check DOUT (GPIO 33) wiring - should connect to mic DOUT");
    LOG_W("Audio", "4. Verify microphone is not damaged");
    LOG_W("Audio", "5. Try speaking loudly into microphone");
    LOG_W("Audio", "========================================");
  }
  
  // Log first non-zero sample when found
  static bool loggedNonZero = false;
  if (!loggedNonZero && samplesRead > 0) {
    for (size_t i = 0; i < samplesRead; i++) {
      if (i2sBuffer[i] != 0x00000000 && i2sBuffer[i] != 0x00000001) {
        loggedNonZero = true;
        Logger::printf(LOG_INFO, "Audio", "✅ First non-zero sample: raw[%d]=0x%08X, converted=%d", 
                       i, i2sBuffer[i], outputBuffer[i]);
        break;
      }
    }
  }
  
  // Return size reflects mono output (half the stereo samples)
  return monoSampleCount * sizeof(int16_t);
}

// ============================================
// STOP RECORDING
// ============================================
void AudioManager::stopRecording() {
  if (currentMode == AUDIO_MODE_RECORDING) {
    LOG_I("Audio", "Stopping recording");
    shutdownI2S();
  }
}

// ============================================
// GET CURRENT MODE
// ============================================
AudioMode AudioManager::getCurrentMode() {
  return currentMode;
}

// ============================================
// CHECK IF ACTIVE
// ============================================
bool AudioManager::isActive() {
  return currentMode != AUDIO_MODE_NONE;
}

// ============================================
// GET PLAYBACK CONFIGURATION
// ============================================
i2s_config_t AudioManager::getPlaybackConfig(uint32_t sampleRate) {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // MAX98357A expects stereo format
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUFFER_COUNT,
    .dma_buf_len = DMA_BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  return config;
}

// ============================================
// GET PLAYBACK PIN CONFIGURATION
// ============================================
i2s_pin_config_t AudioManager::getPlaybackPins() {
  i2s_pin_config_t pins = {
    .bck_io_num = pinAmpBclk,
    .ws_io_num = pinAmpLrclk,
    .data_out_num = pinAmpData,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  return pins;
}

// ============================================
// GET RECORDING CONFIGURATION
// ============================================
i2s_config_t AudioManager::getRecordingConfig(uint32_t sampleRate) {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // SPH0645 outputs 32-bit words
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo frame format
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUFFER_COUNT,
    .dma_buf_len = DMA_BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  return config;
}

// ============================================
// GET RECORDING PIN CONFIGURATION
// ============================================
i2s_pin_config_t AudioManager::getRecordingPins() {
  i2s_pin_config_t pins = {
    .bck_io_num = pinMicBclk,
    .ws_io_num = pinMicLrclk,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = pinMicData
  };
  return pins;
}

// ============================================
// RECONFIGURE I2S
// ============================================
bool AudioManager::reconfigureI2S(AudioMode newMode, uint32_t sampleRate) {
  // Shutdown current configuration if active
  if (currentMode != AUDIO_MODE_NONE) {
    shutdownI2S();
  }
  
  // Get appropriate configuration
  i2s_config_t config;
  i2s_pin_config_t pins;
  
  if (newMode == AUDIO_MODE_PLAYBACK) {
    config = getPlaybackConfig(sampleRate);
    pins = getPlaybackPins();
    LOG_I("Audio", "Configuring I2S for TX (playback)");
  } else if (newMode == AUDIO_MODE_RECORDING) {
    config = getRecordingConfig(sampleRate);
    pins = getRecordingPins();
    LOG_I("Audio", "Configuring I2S for RX (recording)");
  } else {
    LOG_E("Audio", "Invalid audio mode");
    return false;
  }
  
  // Select correct I2S port based on mode
  i2s_port_t i2sPort = (newMode == AUDIO_MODE_RECORDING) ? I2S_PORT_RECORDING : I2S_PORT_PLAYBACK;
  
  // Install I2S driver
  esp_err_t result = i2s_driver_install(i2sPort, &config, 0, NULL);
  if (result != ESP_OK) {
    Logger::printf(LOG_ERROR, "Audio", "i2s_driver_install failed: %d", result);
    return false;
  }
  
  // Set pin configuration
  result = i2s_set_pin(i2sPort, &pins);
  if (result != ESP_OK) {
    Logger::printf(LOG_ERROR, "Audio", "i2s_set_pin failed: %d", result);
    i2s_driver_uninstall(i2sPort);
    return false;
  }
  
  // For RX mode, ESP32 I2S needs explicit start AND may need data flow to generate LRCLK
  // i2s_driver_install() doesn't always start clocks in RX mode
  if (newMode == AUDIO_MODE_RECORDING) {
    result = i2s_start(i2sPort);
    if (result != ESP_OK) {
      Logger::printf(LOG_ERROR, "Audio", "i2s_start failed: %d", result);
      i2s_driver_uninstall(i2sPort);
      return false;
    }
    Logger::printf(LOG_INFO, "Audio", "I2S RX mode started explicitly (I2S_NUM_0)");
    
    // ESP32 I2S RX mode: LRCLK may not toggle until DMA is actively reading
    // Trigger a read to start the DMA and generate LRCLK
    uint8_t dummyBuffer[128];
    size_t bytesRead = 0;
    i2s_read(i2sPort, dummyBuffer, sizeof(dummyBuffer), &bytesRead, 100);  // 100ms timeout
    Logger::printf(LOG_INFO, "Audio", "Triggered initial read (%d bytes) to start LRCLK", bytesRead);
  } else {
    Logger::printf(LOG_INFO, "Audio", "I2S TX mode ready (I2S_NUM_1)");
  }
  
  currentMode = newMode;
  currentSampleRate = sampleRate;
  
  Logger::printf(LOG_INFO, "Audio", "I2S configured: %lu Hz, mode=%d", sampleRate, newMode);
  if (newMode == AUDIO_MODE_RECORDING) {
    Logger::printf(LOG_INFO, "Audio", "I2S pins: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d", 
                   pins.bck_io_num, pins.ws_io_num, pins.data_in_num);
  } else {
    Logger::printf(LOG_INFO, "Audio", "I2S pins: BCLK=GPIO%d, LRCLK=GPIO%d, DATA=GPIO%d", 
                   pins.bck_io_num, pins.ws_io_num, pins.data_out_num);
  }
  
  // Wait for clocks to stabilize
  delay(200);
  Logger::printf(LOG_INFO, "Audio", "I2S driver ready - clocks should be active");
  
  return true;
}

// ============================================
// SHUTDOWN I2S
// ============================================
void AudioManager::shutdownI2S() {
  if (currentMode != AUDIO_MODE_NONE) {
    LOG_D("Audio", "Shutting down I2S");
    // Shutdown the appropriate I2S port
    if (currentMode == AUDIO_MODE_RECORDING) {
      i2s_driver_uninstall(I2S_PORT_RECORDING);
    } else if (currentMode == AUDIO_MODE_PLAYBACK) {
      i2s_driver_uninstall(I2S_PORT_PLAYBACK);
    }
    currentMode = AUDIO_MODE_NONE;
  }
}
