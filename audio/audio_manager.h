/*
 * audio_manager.h
 * 
 * I2S audio manager for microphone and amplifier
 * Handles safe reconfiguration between RX and TX modes
 */

#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include <driver/i2s.h>

// ============================================
// AUDIO MODE
// ============================================
enum AudioMode {
  AUDIO_MODE_NONE,
  AUDIO_MODE_PLAYBACK,
  AUDIO_MODE_RECORDING
};

// ============================================
// AUDIO MANAGER CLASS
// ============================================
class AudioManager {
public:
  // Initialize audio manager (doesn't configure I2S yet)
  bool init(uint8_t bclkPin, uint8_t lrclkPin, uint8_t micDataPin, uint8_t ampDataPin);
  
  // ========================================
  // PLAYBACK FUNCTIONS
  // ========================================
  
  // Start playback mode (reconfigures I2S for TX)
  bool startPlayback(uint32_t sampleRate);
  
  // Write audio data to amplifier
  // Returns number of bytes actually written
  size_t writePlaybackData(const uint8_t* data, size_t length);
  
  // Stop playback
  void stopPlayback();
  
  // ========================================
  // RECORDING FUNCTIONS
  // ========================================
  
  // Start recording mode (reconfigures I2S for RX)
  bool startRecording(uint32_t sampleRate);
  
  // Read recorded audio data
  // Returns number of bytes actually read
  size_t readRecordedData(uint8_t* buffer, size_t maxLength);
  
  // Stop recording
  void stopRecording();
  
  // ========================================
  // STATUS FUNCTIONS
  // ========================================
  
  // Get current audio mode
  AudioMode getCurrentMode();
  
  // Check if audio is active
  bool isActive();

private:
  // Pin assignments
  uint8_t pinBclk;
  uint8_t pinLrclk;
  uint8_t pinMicData;
  uint8_t pinAmpData;
  
  // Current state
  AudioMode currentMode;
  bool initialized;
  uint32_t currentSampleRate;
  
  // I2S configuration helpers
  i2s_config_t getPlaybackConfig(uint32_t sampleRate);
  i2s_pin_config_t getPlaybackPins();
  
  i2s_config_t getRecordingConfig(uint32_t sampleRate);
  i2s_pin_config_t getRecordingPins();
  
  // Safe reconfiguration
  bool reconfigureI2S(AudioMode newMode, uint32_t sampleRate);
  void shutdownI2S();
};

#endif // AUDIO_MANAGER_H
