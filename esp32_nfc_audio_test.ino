/*
 * ESP32 Speaker Test Firmware
 * 
 * Purpose: Test MAX98357A amplifier and speaker only
 * 
 * Features:
 * - Generates test tones (sine wave)
 * - Plays continuously through speaker
 * - No microphone, NFC, or other components needed
 * 
 * Hardware:
 * - ESP32-WROOM-32D
 * - MAX98357A I2S amplifier
 * - Speaker connected to amplifier
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
 // TEST TONE CONFIGURATION
 // ============================================
 #define TEST_FREQUENCY    1000  // Hz (1 kHz test tone)
 #define TONE_DURATION_MS  5000  // Play each tone for 5 seconds
 #define AMPLITUDE         0.5f  // 50% amplitude (to avoid clipping)
 
 // ============================================
 // SETUP
 // ============================================
 void setup() {
   // Initialize serial for debugging
   Serial.begin(115200);
   delay(1000);
   
   LOG_I("Main", "========================================");
   LOG_I("Main", "ESP32 Speaker Test");
   LOG_I("Main", "Testing MAX98357A amplifier only");
   LOG_I("Main", "========================================");
   
   // Initialize Audio (playback only - no microphone needed)
   LOG_I("Main", "Initializing audio playback...");
   bool audioSuccess = audio.init(PIN_I2S_MIC_BCLK, PIN_I2S_MIC_LRCLK, PIN_I2S_MIC_DATA, 
                                  PIN_I2S_AMP_BCLK, PIN_I2S_AMP_LRCLK, PIN_I2S_AMP_DATA);
   
   if (!audioSuccess) {
     LOG_E("Main", "Audio init failed!");
     LOG_E("Main", "Check: 1) I2S wiring, 2) MAX98357A power, 3) GPIO pins");
     while(1) delay(1000);  // Halt on error
   }
   
   LOG_I("Main", "Audio initialized successfully");
   
   // Start playback mode
   if (!audio.startPlayback(SAMPLE_RATE)) {
     LOG_E("Main", "Failed to start playback mode");
     while(1) delay(1000);  // Halt on error
   }
   
   LOG_I("Main", "========================================");
   Logger::printf(LOG_INFO, "Main", "Playing %d Hz test tone...", TEST_FREQUENCY);
   LOG_I("Main", "You should hear a continuous tone from the speaker");
   LOG_I("Main", "========================================");
 }
 
 // ============================================
 // MAIN LOOP
 // ============================================
 void loop() {
   // Generate sine wave test tone
   static uint32_t sampleIndex = 0;
   static int16_t toneBuffer[256];  // Buffer for one chunk of audio
   
   // Generate stereo samples (MAX98357A expects stereo)
   for (int i = 0; i < 128; i++) {  // 128 mono samples = 256 stereo samples
     // Generate sine wave sample
     float phase = 2.0f * PI * TEST_FREQUENCY * sampleIndex / SAMPLE_RATE;
     float sample = sin(phase) * AMPLITUDE;
     
     // Convert to 16-bit PCM
     int16_t pcmSample = (int16_t)(sample * 32767.0f);
     
     // Duplicate to both channels (stereo)
     toneBuffer[i * 2] = pcmSample;      // Left channel
     toneBuffer[i * 2 + 1] = pcmSample;  // Right channel
     
     sampleIndex++;
     if (sampleIndex >= SAMPLE_RATE) {
       sampleIndex = 0;  // Reset to prevent overflow
     }
   }
   
   // Write audio data to I2S
   size_t bytesWritten = audio.writePlaybackData((uint8_t*)toneBuffer, sizeof(toneBuffer));
   
   if (bytesWritten == 0) {
     static unsigned long lastError = 0;
     if (millis() - lastError > 2000) {
       lastError = millis();
       LOG_W("Main", "I2S write returned 0 bytes");
     }
   }
   
   // Small delay to prevent tight loop
   delay(1);
 }
 