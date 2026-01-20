/*
 * app_state.h
 * 
 * Application state machine definitions
 */

#ifndef APP_STATE_H
#define APP_STATE_H

// ============================================
// STATE MACHINE STATES
// ============================================
enum AppState {
  STATE_INIT,          // Initializing hardware
  STATE_IDLE,          // Waiting for button press
  STATE_READING_NFC,   // Reading NFC UID
  STATE_FETCH_AUDIO,   // Downloading audio via HTTP GET
  STATE_PLAYING,       // Playing audio through amplifier
  STATE_RECORDING,     // Recording audio from microphone
  STATE_UPLOADING,     // Uploading audio via HTTP POST
  STATE_ERROR          // Error state (recoverable)
};

// ============================================
// ACTION TYPES (determined by button press)
// ============================================
enum ActionType {
  ACTION_NONE,
  ACTION_PLAYBACK,     // Short press -> play audio
  ACTION_RECORD        // Long press -> record audio
};

// ============================================
// ERROR CODES
// ============================================
enum ErrorCode {
  ERROR_NONE = 0,
  ERROR_NFC_INIT,
  ERROR_NFC_READ,
  ERROR_LTE_INIT,
  ERROR_LTE_NETWORK,
  ERROR_HTTP_GET,
  ERROR_HTTP_POST,
  ERROR_AUDIO_INIT,
  ERROR_AUDIO_PLAYBACK,
  ERROR_AUDIO_RECORD,
  ERROR_OUT_OF_MEMORY
};

#endif // APP_STATE_H
