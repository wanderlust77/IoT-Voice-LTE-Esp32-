# Milestone 1: NFC + Audio Bring-up (No LTE)

## Objective

Test NFC tag detection and local audio recording/playback without any network connectivity.

This milestone validates:
- ✅ NFC reader (PN532) communication and tag detection
- ✅ Button input with debouncing (GPIO34 with external pull-up)
- ✅ I2S microphone recording (SPH0645LM4H)
- ✅ I2S amplifier playback (MAX98357A)
- ✅ Audio buffer management and I2S mode switching

---

## Hardware Requirements

### What You Need

1. **ESP32-WROOM-32D** (DevKitC or similar)
2. **PN532 NFC Reader** (I2C mode)
3. **NTAG213 or NTAG215 NFC tag** (card, sticker, or keyfob)
4. **SPH0645LM4H I2S Microphone** breakout
5. **MAX98357A I2S Amplifier** breakout
6. **Speaker** (4-8 ohm, connect to MAX98357A)
7. **Button** (tactile push button)
8. **10kΩ Pull-up Resistor** (for button on GPIO34)
9. **USB cable** for power and serial debugging

### What You DON'T Need (Yet)

- ❌ LTE modem (not used in this milestone)
- ❌ SIM card (not needed)
- ❌ External 5V supply (ESP32 + peripherals run on USB power)

---

## Wiring

Use the same pinout as the full project (from `hardware_defs.h`):

### NFC (PN532 - I2C Mode)
```
PN532 → ESP32
SDA   → GPIO 21
SCL   → GPIO 19
IRQ   → GPIO 27
RST   → GPIO 32
VCC   → 3.3V
GND   → GND
```

### I2S Microphone (SPH0645LM4H)
```
SPH0645 → ESP32
BCLK    → GPIO 26
LRCLK   → GPIO 25
DOUT    → GPIO 33
SEL     → GND (left channel)
VDD     → 3.3V
GND     → GND
```

### I2S Amplifier (MAX98357A)
```
MAX98357A → ESP32
BCLK      → GPIO 26 (shared with mic)
LRC       → GPIO 25 (shared with mic)
DIN       → GPIO 22
SD        → 3.3V (always enabled)
VIN       → 5V or 3.3V
GND       → GND
```

Connect a **speaker** to the MAX98357A output terminals.

### Button
```
Button → ESP32
One side → GPIO 34
Other side → GND

IMPORTANT: Add 10kΩ pull-up resistor from GPIO34 to 3.3V
(GPIO34 is input-only and lacks internal pull-up)
```

**External Pull-up Circuit:**
```
3.3V ─────┐
          │
         [10kΩ]
          │
GPIO34 ───┴─── Button ─── GND
```

---

## Software Setup

### 1. Open the Test Sketch

In Arduino IDE:
1. **Close** the main `esp32_voice_lte.ino` if it's open
2. **Open** `esp32_nfc_audio_test.ino`
3. This is a standalone simplified firmware for this milestone

### 2. Verify Libraries

Ensure these libraries are installed:
- **Adafruit PN532** (via Library Manager)
- **Adafruit BusIO** (dependency, auto-installs)

### 3. Compile and Upload

1. Select board: **ESP32 Dev Module** or **ESP32 Wrover Module**
2. Select port: Your ESP32's COM port
3. Click **Upload**

### 4. Open Serial Monitor

- **Baud rate:** 115200
- You should see initialization messages

---

## Expected Behavior

### Startup Sequence

Upon reset, you should see:
```
========================================
ESP32 NFC + Audio Test
Milestone: Local bring-up (no LTE)
========================================
[INFO] [Main] Allocating audio buffer...
[INFO] [Main] Audio buffer: 80000 samples (5.0 seconds max)
[INFO] [Main] Initializing button...
[INFO] [Main] NOTE: GPIO34 requires EXTERNAL pull-up resistor!
[INFO] [Main] Initializing NFC...
[INFO] [NFC] PN532 firmware version: X.X
[INFO] [Main] Initializing audio...
[INFO] [Audio] I2S configured for 16kHz, 16-bit
========================================
Initialization complete!

Usage:
1. Present NFC tag to reader
2. SHORT press button → Record 3 seconds
3. LONG press button → Playback recording
========================================
```

### Workflow

1. **Present NFC tag** to the PN532 reader
   - You should see: `NFC Tag Detected: AA:BB:CC:DD`
   - Firmware enters NFC read state

2. **Short-press button** (< 2 seconds)
   - Starts recording audio for 3 seconds
   - Speak into the microphone
   - You'll see progress: `Recording... 0.5s / 3.0s`
   - After 3 seconds: `Recording complete! Captured XXXXX samples`

3. **Long-press button** (> 2 seconds)
   - Plays back the recorded audio through the speaker
   - You'll see progress: `Playback... 50%`
   - After playback: `Playback complete!`

4. **Repeat** as needed (short-press to record again, long-press to play)

---

## Troubleshooting

### NFC Not Detecting

**Symptom:** No "NFC Tag Detected" message when presenting tag

**Check:**
1. I2C wiring (SDA, SCL, GND)
2. PN532 power (3.3V)
3. PN532 is in **I2C mode** (check DIP switches or solder jumpers on breakout)
4. IRQ and RST pins connected
5. Tag is NTAG213/215 (not MIFARE Classic, which uses different protocol)

**Test:** Run Phase 3 test from `TESTING_GUIDE.md` to isolate NFC

### Button Not Responding

**Symptom:** No response when pressing button

**Check:**
1. **CRITICAL:** Is 10kΩ pull-up resistor installed? GPIO34 NEEDS external pull-up!
2. Button wiring (one side to GPIO34, other to GND)
3. Button is not defective (test continuity with multimeter)

**Test:** Add debug output:
```cpp
void loop() {
  button.update();
  Serial.println(digitalRead(PIN_BUTTON));  // Should be HIGH when not pressed
  delay(100);
}
```
- Not pressed: Should show `1` (HIGH)
- Pressed: Should show `0` (LOW)
- If always `0`: Missing pull-up resistor!

### No Audio Recording

**Symptom:** "Recording complete" but playback is silent or garbled

**Check:**
1. Microphone wiring (BCLK, LRCLK, DOUT)
2. Microphone power (3.3V)
3. SEL pin on microphone is connected to GND (left channel)
4. Microphone is oriented correctly (sound port facing you)

**Test:** Check if samples are non-zero:
```cpp
// After recording
for (int i = 0; i < 100; i++) {
  Serial.println(audioBuffer[i]);
}
// Should see varying values, not all zeros
```

### No Audio Playback

**Symptom:** Playback runs but no sound from speaker

**Check:**
1. Amplifier wiring (BCLK, LRC, DIN)
2. Amplifier power (VIN to 5V or 3.3V)
3. SD pin on amplifier HIGH (connected to 3.3V)
4. Speaker connected to amplifier output
5. Speaker impedance (4-8 ohm)
6. Volume (some MAX98357A have gain jumpers)

**Test:** Generate a test tone:
```cpp
// Fill buffer with 440Hz sine wave
for (int i = 0; i < 1000; i++) {
  audioBuffer[i] = (int16_t)(sin(2 * PI * 440 * i / SAMPLE_RATE) * 10000);
}
recordedSamples = 1000;
// Then trigger playback
```

### Heap/Memory Issues

**Symptom:** "Failed to allocate audio buffer" or crashes

**Cause:** Not enough free heap for 80000-sample buffer (160KB)

**Solution:** Reduce buffer size in firmware:
```cpp
#define MAX_AUDIO_SAMPLES  (SAMPLE_RATE * 3)  // 3 seconds instead of 5
```

---

## Success Criteria

✅ This milestone is complete when:

1. NFC tag UID is read and displayed correctly
2. Button short-press triggers 3-second recording
3. Button long-press plays back recorded audio clearly through speaker
4. Can repeat record/playback cycle multiple times without errors
5. No crashes or memory issues

---

## Next Steps

Once this milestone is working:
- **Milestone 2:** Add LTE modem communication (AT commands, network attach)
- **Milestone 3:** Add HTTP GET to fetch audio from server using NFC UID
- **Milestone 4:** Add HTTP POST to upload recorded audio
- **Milestone 5:** Full integration and error handling

---

## Notes

- Audio is stored in **RAM only** (lost on reset)
- Max recording: 5 seconds (configurable)
- Sample rate: 16 kHz, 16-bit mono
- No network activity in this milestone
- LTE modem can remain disconnected

This simplified firmware helps isolate hardware issues before adding network complexity!
