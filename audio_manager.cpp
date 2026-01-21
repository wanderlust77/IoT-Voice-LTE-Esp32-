/*
 * audio_manager.cpp
 * 
 * Implementation of I2S audio manager
 */

#include "audio_manager.h"
#include "logger.h"
#include "config.h"

// I2S port number (ESP32 has 2 I2S ports: 0 and 1)
#define I2S_PORT I2S_NUM_0

// ============================================
// INITIALIZE AUDIO MANAGER
// ============================================
bool AudioManager::init(uint8_t bclkPin, uint8_t lrclkPin, uint8_t micDataPin, uint8_t ampDataPin) {
  pinBclk = bclkPin;
  pinLrclk = lrclkPin;
  pinMicData = micDataPin;
  pinAmpData = ampDataPin;
  
  currentMode = AUDIO_MODE_NONE;
  initialized = true;
  currentSampleRate = SAMPLE_RATE;
  
  LOG_I("Audio", "Audio manager initialized");
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
  esp_err_t result = i2s_write(I2S_PORT, data, length, &bytesWritten, portMAX_DELAY);
  
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
    i2s_zero_dma_buffer(I2S_PORT);
    
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
  i2s_zero_dma_buffer(I2S_PORT);
  
  // Wait a bit for microphone to stabilize and start outputting data
  delay(100);
  
  LOG_I("Audio", "Recording mode ready");
  Logger::printf(LOG_INFO, "Audio", "I2S configured: %lu Hz, 32-bit, RX mode, pins: BCLK=%d, LRCLK=%d, DATA=%d", 
                 sampleRate, pinBclk, pinLrclk, pinMicData);
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
  esp_err_t result = i2s_read(I2S_PORT, i2sBuffer, bytesToRead, &bytesRead, 0);  // Non-blocking
  
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
  
  // Log raw values periodically (every 50 reads with data)
  static int dataReadCount = 0;
  dataReadCount++;
  if (dataReadCount <= 3 || dataReadCount % 50 == 0) {
    Logger::printf(LOG_INFO, "Audio", "Data read #%d: samples=%d, raw[0]=%08X, raw[1]=%08X", 
                   dataReadCount, samplesRead, i2sBuffer[0], i2sBuffer[1]);
  }
  
  for (size_t i = 0; i < samplesRead; i++) {
    uint32_t sample32 = i2sBuffer[i];
    
    // SPH0645LM4H outputs 32-bit words with 18-bit audio data
    // The exact format varies, so try different extraction methods
    
    int32_t audioData;
    
    // Method 1: SPH0645 typically has data in upper 18 bits (bits 14-31)
    // Take upper 16 bits directly (most common format)
    audioData = (int32_t)((int16_t)(sample32 >> 16));
    
    // Method 2: If that doesn't work, try lower 18 bits (bits 0-17)
    // Uncomment this if Method 1 gives poor results:
    // audioData = (int32_t)((int16_t)(sample32 & 0x3FFFF) >> 2);
    
    // Clamp to 16-bit range (shouldn't be needed but safe)
    if (audioData > 32767) audioData = 32767;
    if (audioData < -32768) audioData = -32768;
    
    outputBuffer[i] = (int16_t)audioData;
  }
  
  return samplesRead * sizeof(int16_t);  // Return bytes of 16-bit samples
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
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
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
    .bck_io_num = pinBclk,
    .ws_io_num = pinLrclk,
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
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // SPH0645 outputs 32-bit!
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
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
    .bck_io_num = pinBclk,
    .ws_io_num = pinLrclk,
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
  
  // Install I2S driver
  esp_err_t result = i2s_driver_install(I2S_PORT, &config, 0, NULL);
  if (result != ESP_OK) {
    Logger::printf(LOG_ERROR, "Audio", "i2s_driver_install failed: %d", result);
    return false;
  }
  
  // Set pin configuration
  result = i2s_set_pin(I2S_PORT, &pins);
  if (result != ESP_OK) {
    Logger::printf(LOG_ERROR, "Audio", "i2s_set_pin failed: %d", result);
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }
  
  currentMode = newMode;
  currentSampleRate = sampleRate;
  
  Logger::printf(LOG_INFO, "Audio", "I2S configured: %lu Hz, mode=%d", sampleRate, newMode);
  return true;
}

// ============================================
// SHUTDOWN I2S
// ============================================
void AudioManager::shutdownI2S() {
  if (currentMode != AUDIO_MODE_NONE) {
    LOG_D("Audio", "Shutting down I2S");
    i2s_driver_uninstall(I2S_PORT);
    currentMode = AUDIO_MODE_NONE;
  }
}
