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
#define PIN_I2S_BCLK      26  // Bit clock (shared by mic and amp)
#define PIN_I2S_LRCLK     25  // Left/Right clock (shared by mic and amp)
#define PIN_I2S_MIC_DATA  33  // SPH0645LM4H microphone data input
#define PIN_I2S_AMP_DATA  22  // MAX98357A amplifier data output

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
#define PIN_LTE_RESET     23  // Reset pin (optional)

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
