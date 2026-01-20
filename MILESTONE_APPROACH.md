# Phased Milestone Approach

## Overview

Instead of testing everything at once, we're breaking down the firmware into testable milestones. This allows:
- ✅ Incremental hardware validation
- ✅ Easier debugging (isolate subsystems)
- ✅ Working firmware at each stage
- ✅ Confidence before adding complexity

---

## Milestone 1: NFC + Audio (Local Only) ⭐ CURRENT

**Firmware:** `esp32_nfc_audio_test.ino`

**Goal:** Validate NFC and audio hardware work correctly

**Features:**
- Read NFC tag UID (NTAG213/215)
- Record audio (3 seconds) on button short-press
- Playback recorded audio on button long-press
- All local (stored in RAM buffer)
- **NO LTE, NO NETWORK**

**Hardware Used:**
- ESP32
- PN532 NFC reader
- SPH0645LM4H microphone
- MAX98357A amplifier
- Button (with 10kΩ pull-up on GPIO34)
- USB power only

**Success Criteria:**
- NFC tag detected and UID displayed
- Button press detected correctly (short vs long)
- Audio records clearly from microphone
- Audio plays back clearly through speaker
- Stable operation, no crashes

**Documentation:** See `MILESTONE_NFC_AUDIO.md`

---

## Milestone 2: LTE Communication (No Audio)

**Goal:** Validate LTE modem UART communication and network attach

**Features:**
- Initialize LTE modem (UART AT commands)
- Power on modem correctly
- Unlock SIM with PIN if needed
- Attach to network (Cat-M or NB-IoT)
- Basic AT command testing
- **NO AUDIO, NO HTTP YET**

**Hardware Added:**
- MIKROE-6287 LTE modem
- External 5V power supply for modem
- UART connection (GPIO16/17)

**Success Criteria:**
- Modem responds to AT commands
- SIM unlocked successfully
- Network registration successful
- Signal strength readable
- Stable UART communication

---

## Milestone 3: HTTP GET (Fetch Audio by UID)

**Goal:** Fetch audio file from server using NFC UID

**Features:**
- Combine NFC + LTE
- Read NFC tag → Get UID
- Connect to network
- HTTP GET: `http://server.com/audio?uid=AABBCCDD`
- Download audio data
- Play through speaker
- **NO RECORDING/UPLOAD YET**

**Success Criteria:**
- HTTP GET request completes successfully
- Audio data downloaded correctly
- Downloaded audio plays through speaker
- Error handling for network failures

---

## Milestone 4: HTTP POST (Upload Recorded Audio)

**Goal:** Upload recorded audio with NFC UID

**Features:**
- Record audio from microphone
- Read NFC UID
- HTTP POST: Send audio data + UID to server
- Handle response from server
- Retry logic for failures

**Success Criteria:**
- Audio uploads successfully
- Server acknowledges receipt
- Retry logic works on network errors
- Memory managed correctly (no leaks)

---

## Milestone 5: Full Integration

**Goal:** Complete product firmware with all features

**Features:**
- Full state machine (as in original plan)
- Short press → Fetch & play audio
- Long press → Record & upload audio
- Error handling and recovery
- Production-ready reliability

**Success Criteria:**
- Both workflows (fetch/play and record/upload) work reliably
- Error states handled gracefully
- System can run for extended periods
- Ready for real-world testing

---

## File Organization

### Test Firmware (Milestones)
- `esp32_nfc_audio_test.ino` - Milestone 1
- `esp32_lte_test.ino` - Milestone 2 (to be created)
- More test sketches as needed...

### Production Firmware
- `esp32_voice_lte.ino` - Final integrated firmware (Milestone 5)

### Shared Modules (Used by All)
- `config.h` - Configuration constants
- `hardware_defs.h` - Pin definitions
- `app_state.h` - State machine enums
- `logger.h/cpp` - Logging utility
- `button_handler.h/cpp` - Button input
- `nfc_manager.h/cpp` - NFC interface
- `audio_manager.h/cpp` - I2S audio
- `lte_manager.h/cpp` - LTE modem & HTTP

---

## Current Status

✅ **Milestone 1 firmware created:** `esp32_nfc_audio_test.ino`

**Next Steps:**
1. Wire up NFC + Audio + Button hardware
2. Ensure GPIO34 has external 10kΩ pull-up resistor
3. Upload `esp32_nfc_audio_test.ino` to ESP32
4. Open Serial Monitor (115200 baud)
5. Present NFC tag and test record/playback

**LTE modem can remain disconnected** for this milestone - we'll add it in Milestone 2!

---

## Why This Approach?

### Problems with "Big Bang" Testing:
- ❌ Too many unknowns at once
- ❌ Hard to isolate issues
- ❌ One broken component blocks everything
- ❌ Frustrating debugging experience

### Benefits of Phased Milestones:
- ✅ One subsystem at a time
- ✅ Clear success/failure criteria
- ✅ Working firmware at each stage
- ✅ Build confidence incrementally
- ✅ Faster overall progress

---

## Troubleshooting Strategy

Each milestone has its own troubleshooting guide:
- Milestone 1: `MILESTONE_NFC_AUDIO.md`
- LTE issues: `LTE_TROUBLESHOOTING.md`
- General tests: `TESTING_GUIDE.md`

**Focus on getting Milestone 1 working first!**
