/*
 * hardware_defs.h
 * 
 * FIXED HARDWARE PIN DEFINITIONS
 * DO NOT MODIFY - These are determined by physical wiring
 */

#ifndef HARDWARE_DEFS_H
#define HARDWARE_DEFS_H

// ============================================
// I2S AUDIO PINS
// ============================================
// Microphone (I2S_NUM_0) - Recording
#define PIN_I2S_MIC_BCLK      26  // Bit clock for microphone
#define PIN_I2S_MIC_LRCLK     25  // Left/Right clock for microphone
#define PIN_I2S_MIC_DATA      33  // SPH0645LM4H microphone data input

// Amplifier (I2S_NUM_1) - Playback
// NOTE: GPIO 12 and 13 are strapping pins but can be used after boot
// If you prefer different pins, change these values (must be output-capable GPIOs)
#define PIN_I2S_AMP_BCLK      12  // Bit clock for amplifier (separate from mic)
#define PIN_I2S_AMP_LRCLK     13  // Left/Right clock for amplifier (separate from mic)
#define PIN_I2S_AMP_DATA      22  // MAX98357A amplifier data output

// Legacy aliases for backward compatibility (use mic pins)
#define PIN_I2S_BCLK      PIN_I2S_MIC_BCLK
#define PIN_I2S_LRCLK     PIN_I2S_MIC_LRCLK

// ============================================
// NFC PN532 (I2C MODE)
// ============================================
#define PIN_NFC_SDA       21  // I2C data
#define PIN_NFC_SCL       19  // I2C clock
#define PIN_NFC_IRQ       27  // Interrupt request
#define PIN_NFC_RST       32  // Reset

// ============================================
// LTE MODEM (MIKROE-6287, UART2)
// ============================================
#define PIN_LTE_TX        17  // ESP32 TX -> Modem RX
#define PIN_LTE_RX        16  // ESP32 RX <- Modem TX
#define PIN_LTE_PWRKEY    18  // Power key (pulse to turn on/off)
#define PIN_LTE_RESET     23  // Reset pin
#define PIN_LTE_CTS       4   // CTS flow control (not used, strapping pin)

// ============================================
// USER INPUT
// ============================================
#define PIN_BUTTON        34  // Button (INPUT_PULLUP, input-only pin)

// ============================================
// STRAPPING PINS - DO NOT USE
// ============================================
// GPIO 0, 2, 4, 5, 12, 13, 14, 15
// These pins affect boot mode and should be avoided

#endif // HARDWARE_DEFS_H
