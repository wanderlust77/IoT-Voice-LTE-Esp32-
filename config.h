/*
 * config.h
 * 
 * USER-CONFIGURABLE SETTINGS
 * Modify these values to tune system behavior
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ============================================
// AUDIO CONFIGURATION
// ============================================
#define SAMPLE_RATE           16000  // Hz (16 kHz - optimized for memory constraints)
#define BITS_PER_SAMPLE       16     // 16-bit PCM
#define DMA_BUFFER_COUNT      4      // Number of DMA buffers
#define DMA_BUFFER_SIZE       512    // Samples per DMA buffer

// Audio buffer sizes (in bytes)
#define AUDIO_BUFFER_SIZE     32768  // 32KB = ~1 second at 16kHz 16-bit mono

// ============================================
// LTE MODEM CONFIGURATION
// ============================================
#define LTE_BAUD_RATE         115200
#define LTE_APN               "internet.ht.ht"          // HT Mobile (Croatia) APN
#define LTE_PIN               "5576"                   // SIM PIN (optional, set to "" if no PIN)
#define API_ENDPOINT          "http://yourserver.com"  // Replace with your server
// SIM Info (for reference):
// IMEI: 97232302079745
// Phone: +385 97 684 9343

// ============================================
// TIMING CONFIGURATION
// ============================================
#define LONG_PRESS_MS         800    // ms - button hold time for long press
#define DEBOUNCE_MS           50     // ms - button debounce time
#define NFC_READ_TIMEOUT_MS   2000   // ms - timeout for NFC read operation
#define LTE_COMMAND_TIMEOUT_MS 5000  // ms - timeout for AT commands
#define LTE_HTTP_TIMEOUT_MS   15000  // ms - timeout for HTTP operations
#define MAX_RECORDING_MS      30000  // ms - maximum recording duration (30 seconds)

// ============================================
// NETWORK CONFIGURATION
// ============================================
#define NETWORK_ATTACH_RETRIES  3    // Number of network attach attempts
#define HTTP_RETRY_COUNT        3    // Number of HTTP request retries

// ============================================
// SERIAL DEBUG
// ============================================
#define SERIAL_BAUD_RATE      115200
#define ENABLE_DEBUG_LOGGING  true

// ============================================
// MEMORY MANAGEMENT
// ============================================
#define MIN_FREE_HEAP         50000  // Minimum free heap before warning (bytes)

#endif // CONFIG_H
