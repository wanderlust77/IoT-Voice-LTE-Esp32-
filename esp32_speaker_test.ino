/*
 * ESP32 Speaker Test Firmware
 * 
 * Tests MAX98357A amplifier and speaker with generated audio
 * 
 * Features:
 * - Generates sine wave tones (440Hz, 1000Hz, etc.)
 * - Generates random noise
 * - Tests different volume levels
 * - Continuous playback for testing
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - MAX98357A I2S amplifier
 * - Speaker (4-8 ohm)
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
// TEST CONFIGURATION
// ============================================
#define TEST_DURATION_SECONDS  5   // How long each test tone plays
#define SINE_WAVE_AMPLITUDE     10000  // Amplitude for sine waves (0-32767)

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  
  LOG_I("Main", "========================================");
  LOG_I("Main", "ESP32 Speaker Test");
  LOG_I("Main", "Testing MAX98357A amplifier + speaker");
  LOG_I("Main", "========================================");
  
  // Initialize Audio
  LOG_I("Main", "Initializing audio...");
  if (!audio.init(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_MIC_DATA, PIN_I2S_AMP_DATA)) {
    LOG_E("Main", "Audio init failed!");
    while(1) delay(1000);  // Halt
  }
  
  LOG_I("Main", "Audio initialized successfully");
  LOG_I("Main", "");
  LOG_I("Main", "Starting speaker tests...");
  LOG_I("Main", "Each test plays for 5 seconds");
  LOG_I("Main", "========================================");
  Serial.println();
  
  // Start playback mode
  if (!audio.startPlayback(SAMPLE_RATE)) {
    LOG_E("Main", "Failed to start playback mode!");
    while(1) delay(1000);  // Halt
  }
  
  LOG_I("Main", "Playback mode ready");
  delay(500);
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  static int testNumber = 0;
  static unsigned long testStartTime = 0;
  static bool testStarted = false;
  
  // Start new test
  if (!testStarted) {
    testStartTime = millis();
    testStarted = true;
    testNumber++;
    
    LOG_I("Main", "");
    LOG_I("Main", "========================================");
    Logger::printf(LOG_INFO, "Main", "TEST #%d", testNumber);
    LOG_I("Main", "========================================");
  }
  
  // Run current test
  unsigned long elapsed = (millis() - testStartTime) / 1000;
  
  switch (testNumber) {
    case 1:
      testSineWave(440, "440Hz (A4 note)", elapsed);
      break;
    case 2:
      testSineWave(1000, "1000Hz tone", elapsed);
      break;
    case 3:
      testSineWave(2000, "2000Hz tone", elapsed);
      break;
    case 4:
      testRandomNoise(elapsed);
      break;
    case 5:
      testVolumeSweep(elapsed);
      break;
    case 6:
      testSilence(elapsed);
      break;
    default:
      // All tests complete - restart
      LOG_I("Main", "");
      LOG_I("Main", "========================================");
      LOG_I("Main", "All tests complete! Restarting...");
      LOG_I("Main", "========================================");
      delay(2000);
      testNumber = 0;
      testStarted = false;
      return;
  }
  
  // Check if test duration elapsed
  if (elapsed >= TEST_DURATION_SECONDS) {
    audio.stopPlayback();
    delay(500);
    audio.startPlayback(SAMPLE_RATE);
    delay(500);
    testStarted = false;
  }
  
  delay(10);
}

// ============================================
// TEST FUNCTIONS
// ============================================

// Test 1-3: Sine wave tones
void testSineWave(int frequency, const char* description, unsigned long elapsed) {
  static unsigned long lastLog = 0;
  if (elapsed == 0) {
    Logger::printf(LOG_INFO, "Main", "Playing sine wave: %s", description);
    Logger::printf(LOG_INFO, "Main", "Frequency: %d Hz, Amplitude: %d", frequency, SINE_WAVE_AMPLITUDE);
  }
  
  // Generate one chunk of sine wave samples
  static int16_t buffer[512];
  static float phase = 0.0f;
  
  for (int i = 0; i < 512; i++) {
    buffer[i] = (int16_t)(sin(phase) * SINE_WAVE_AMPLITUDE);
    phase += 2.0f * PI * frequency / SAMPLE_RATE;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;
  }
  
  // Send to amplifier
  size_t bytesWritten = audio.writePlaybackData((uint8_t*)buffer, sizeof(buffer));
  
  // Log progress every second
  if (elapsed != lastLog) {
    lastLog = elapsed;
    Logger::printf(LOG_INFO, "Main", "Playing... %lu/%d seconds", elapsed, TEST_DURATION_SECONDS);
  }
}

// Test 4: Random noise
void testRandomNoise(unsigned long elapsed) {
  static unsigned long lastLog = 0;
  if (elapsed == 0) {
    LOG_I("Main", "Playing random noise (white noise)");
    LOG_I("Main", "You should hear static/hiss");
  }
  
  // Generate random samples
  static int16_t buffer[512];
  for (int i = 0; i < 512; i++) {
    buffer[i] = (int16_t)(random(-SINE_WAVE_AMPLITUDE, SINE_WAVE_AMPLITUDE));
  }
  
  // Send to amplifier
  audio.writePlaybackData((uint8_t*)buffer, sizeof(buffer));
  
  // Log progress every second
  if (elapsed != lastLog) {
    lastLog = elapsed;
    Logger::printf(LOG_INFO, "Main", "Playing... %lu/%d seconds", elapsed, TEST_DURATION_SECONDS);
  }
}

// Test 5: Volume sweep (sine wave with increasing amplitude)
void testVolumeSweep(unsigned long elapsed) {
  static unsigned long lastLog = 0;
  if (elapsed == 0) {
    LOG_I("Main", "Playing volume sweep (440Hz, increasing volume)");
    LOG_I("Main", "Volume should gradually increase");
  }
  
  // Calculate current amplitude based on elapsed time
  float progress = (float)elapsed / TEST_DURATION_SECONDS;
  int amplitude = (int)(progress * SINE_WAVE_AMPLITUDE);
  if (amplitude > SINE_WAVE_AMPLITUDE) amplitude = SINE_WAVE_AMPLITUDE;
  
  // Generate sine wave with current amplitude
  static int16_t buffer[512];
  static float phase = 0.0f;
  const int frequency = 440;
  
  for (int i = 0; i < 512; i++) {
    buffer[i] = (int16_t)(sin(phase) * amplitude);
    phase += 2.0f * PI * frequency / SAMPLE_RATE;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;
  }
  
  // Send to amplifier
  audio.writePlaybackData((uint8_t*)buffer, sizeof(buffer));
  
  // Log progress every second
  if (elapsed != lastLog) {
    lastLog = elapsed;
    Logger::printf(LOG_INFO, "Main", "Playing... %lu/%d seconds (amplitude: %d)", 
                   elapsed, TEST_DURATION_SECONDS, amplitude);
  }
}

// Test 6: Silence (verify speaker stops)
void testSilence(unsigned long elapsed) {
  static unsigned long lastLog = 0;
  if (elapsed == 0) {
    LOG_I("Main", "Playing silence (all zeros)");
    LOG_I("Main", "Speaker should be quiet");
  }
  
  // Generate silence (all zeros)
  static int16_t buffer[512];
  memset(buffer, 0, sizeof(buffer));
  
  // Send to amplifier
  audio.writePlaybackData((uint8_t*)buffer, sizeof(buffer));
  
  // Log progress every second
  if (elapsed != lastLog) {
    lastLog = elapsed;
    Logger::printf(LOG_INFO, "Main", "Playing... %lu/%d seconds", elapsed, TEST_DURATION_SECONDS);
  }
}
