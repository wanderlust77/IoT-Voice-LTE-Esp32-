# ESP32 Voice LTE Firmware

Firmware for an ESP32-based prototype device that enables NFC-triggered voice recording and playback over LTE connectivity.

## Hardware Requirements

### Components
- **ESP32 DevKitC-32E** - Main controller
- **MIKROE-6287** - LTE Cat-M modem (5V powered, UART AT commands)
- **PN532** - NFC reader (I2C mode)
- **SPH0645LM4H** - I2S MEMS microphone
- **MAX98357A** - I2S audio amplifier
- **Button** - User input (momentary push button)
- **Power**: Computer USB only (5V, no battery)

### Wiring (DO NOT MODIFY)

#### I2S Audio
- **BCLK** → GPIO26 (shared by mic and amp)
- **LRCLK** → GPIO25 (shared by mic and amp)
- **MIC DATA IN** → GPIO33 (SPH0645LM4H)
- **AMP DATA OUT** → GPIO22 (MAX98357A)

#### NFC (PN532, I2C Mode)
- **SDA** → GPIO21
- **SCL** → GPIO19
- **IRQ** → GPIO27
- **RST** → GPIO32

#### LTE Modem (UART2)
- **ESP32 TX** (GPIO17) → **LTE RX**
- **ESP32 RX** (GPIO16) → **LTE TX**
- **PWRKEY** → GPIO18
- **RESET** → GPIO23

#### Button
- **BUTTON** → GPIO34 (INPUT only, active LOW)

#### Power
- ESP32 powered via USB
- LTE modem powered from USB 5V rail (**NOT** from ESP32 3.3V!)
- All grounds must be connected together

#### Important Notes
- GPIO 34 is input-only (no internal pull-up, use external pull-up resistor)
- Strapping pins avoided: GPIO 0, 2, 4, 5, 12, 13, 14, 15

## Software Requirements

### Arduino IDE
- Install [Arduino IDE](https://www.arduino.cc/en/software) 1.8.x or 2.x
- Install ESP32 board support:
  - Add `https://dl.espressif.com/dl/package_esp32_index.json` to Additional Board Manager URLs
  - Install "esp32" by Espressif Systems

### Required Libraries
Install via Arduino Library Manager:
- **Adafruit PN532** (for NFC reader)
- Built-in libraries (no install needed):
  - Wire (I2C)
  - SPI
  - driver/i2s (ESP32 I2S)
  - HardwareSerial

## Configuration

### Before First Upload

Edit `config.h` to configure your settings:

```cpp
// LTE Configuration
#define LTE_APN "your.apn.here"           // Your carrier's APN
#define API_ENDPOINT "http://yourserver.com"  // Your backend server

// Audio Settings (default values are optimized for voice)
#define SAMPLE_RATE 16000                 // 16 kHz
#define AUDIO_BUFFER_SIZE 32768          // 32 KB (~1 second)

// Timing
#define LONG_PRESS_MS 800                // Long press threshold
#define MAX_RECORDING_MS 30000           // Max recording duration (30s)
```

### Backend API Requirements

Your backend server must implement two endpoints:

#### 1. GET /audio?uid={NFC_UID}
- Returns audio data as raw PCM (16-bit, 16 kHz, mono)
- Content-Type: `application/octet-stream`
- Example: `http://yourserver.com/audio?uid=ABCD1234`

#### 2. POST /upload?uid={NFC_UID}
- Accepts audio data as raw PCM (16-bit, 16 kHz, mono)
- Content-Type: `application/octet-stream`
- Body: Raw binary audio data
- Example: `http://yourserver.com/upload?uid=ABCD1234`

## Building and Uploading

### Arduino IDE Setup
1. Open `esp32_voice_lte.ino` in Arduino IDE
2. Select Board: **ESP32 Dev Module**
3. Configure board settings:
   - Upload Speed: 921600
   - Flash Frequency: 80MHz
   - Flash Mode: QIO
   - Flash Size: 4MB
   - Partition Scheme: Default 4MB with spiffs
   - Core Debug Level: None (or Info for debugging)
   - Port: Select your ESP32's COM port

### Upload
1. Connect ESP32 via USB
2. Click **Upload** button
3. Wait for compilation and upload to complete

### Monitor Serial Output
1. Open Serial Monitor (Tools → Serial Monitor)
2. Set baud rate to **115200**
3. You should see initialization messages

## Operation

### User Interface

The device is controlled by a single button:

- **Short Press** (< 800ms): Play audio
  1. Tap NFC card
  2. Device fetches audio from server using NFC UID
  3. Audio plays through speaker

- **Long Press** (≥ 800ms, hold button): Record audio
  1. Hold button
  2. Tap NFC card (while still holding button)
  3. Speak into microphone (while holding button)
  4. Release button to stop recording
  5. Device uploads audio to server using NFC UID

### LED Feedback
- Use Serial Monitor for debugging (115200 baud)
- All operations are logged with timestamps and log levels

## Testing Procedure

Follow this incremental testing sequence to verify each subsystem:

### Phase 1: Basic Hardware
```cpp
// Test: Flash a blink sketch first
void setup() {
  pinMode(2, OUTPUT);
}
void loop() {
  digitalWrite(2, !digitalRead(2));
  delay(1000);
}
```
✓ Verify ESP32 is programmable and functioning

### Phase 2: Serial Logging
Upload the firmware and open Serial Monitor (115200 baud):
```
Expected output:
===================================
ESP32 Voice LTE - Logger Initialized
===================================
[...] [INFO] [Main] ESP32 Voice LTE - Starting up
```

### Phase 3: NFC Reader
1. Upload firmware
2. Open Serial Monitor
3. Hold NFC card near reader
4. Press button
```
Expected output:
[...] [INFO] [NFC] Found PN532 chip, firmware v1.6
[...] [INFO] [Main] Reading NFC UID...
[...] [INFO] [NFC] UID read: ABCD1234 (4 bytes)
```

### Phase 4: LTE Modem
Monitor serial output during boot:
```
Expected output:
[...] [INFO] [LTE] Initializing LTE modem...
[...] [INFO] [LTE] Powering on modem...
[...] [DEBUG] [LTE] TX: AT
[...] [DEBUG] [LTE] RX: OK
[...] [INFO] [LTE] Modem powered on successfully
[...] [INFO] [LTE] Checking network registration...
[...] [INFO] [LTE] Network registered
```

### Phase 5: Button Input
Press button and observe:
```
Expected output:
[...] [INFO] [Main] Short press detected -> PLAYBACK
or
[...] [INFO] [Main] Long press detected -> RECORD
```

### Phase 6: Audio Playback
1. Ensure backend server is running
2. Place audio file on server at `/audio?uid=YOUR_NFC_UID`
3. Tap NFC card
4. Short press button
```
Expected output:
[...] [INFO] [Main] Fetching audio from server...
[...] [INFO] [LTE] HTTP GET complete: 32000 bytes
[...] [INFO] [Main] Playing audio...
[...] [INFO] [Audio] I2S configured for TX (playback)
[...] [INFO] [Main] Playback complete
```

### Phase 7: Audio Recording
1. Ensure backend server is running
2. Tap NFC card
3. Long press button, speak, then release
```
Expected output:
[...] [INFO] [Main] Starting recording...
[...] [INFO] [Audio] I2S configured for RX (recording)
[...] [INFO] [Main] Recording... (release button to stop)
[...] [INFO] [Main] Recording stopped: 32000 bytes, 2000 ms
[...] [INFO] [Main] Uploading audio to server...
[...] [INFO] [LTE] HTTP POST complete
```

## Troubleshooting

### NFC Reader Not Detected
- Check I2C wiring (SDA, SCL, GND)
- Run I2C scanner to verify 0x24 address
- Check PN532 is in I2C mode (switches on board)
- Verify power supply (3.3V)

### LTE Modem Not Responding
- Verify modem is powered from USB 5V, not ESP32 3.3V
- Check UART wiring (TX/RX crossed correctly)
- Increase AT command timeout in `config.h`
- Check SIM card is inserted and activated
- Verify APN configuration matches your carrier

### No Network Registration
- Check SIM card status (AT+CPIN?)
- Verify APN is correct for your carrier
- Check cellular signal strength
- Try different location (near window)
- Check SIM card has data plan

### Audio Playback Issues
- Verify I2S wiring (BCLK, LRCLK, DATA)
- Check MAX98357A power supply
- Verify speaker is connected to amplifier
- Check audio format is raw PCM, 16-bit, 16kHz, mono
- Test with known-good audio file (e.g., sine wave)

### Audio Recording Issues
- Verify microphone wiring
- Check microphone power (3.3V)
- Increase recording gain if needed
- Test in quiet environment first
- Note: SPH0645 outputs 32-bit data (firmware converts to 16-bit)

### Memory Issues
```
[...] [ERROR] [Main] Failed to allocate audio buffer!
```
- Reduce `AUDIO_BUFFER_SIZE` in `config.h`
- Check for memory leaks
- Avoid using large local variables
- Consider using PSRAM if available (not on DevKitC-32E)

### HTTP Request Failures
- Verify backend server is running and accessible
- Check URL format in `config.h`
- Test server endpoint with curl or Postman
- Check network registration status
- Verify bearer profile is configured
- Increase HTTP timeout in `config.h`

## Architecture Overview

### State Machine Flow
```
INIT → IDLE → READING_NFC → FETCH_AUDIO → PLAYING → IDLE
              READING_NFC → RECORDING → UPLOADING → IDLE
```

### Module Structure
```
esp32_voice_lte/
├── esp32_voice_lte.ino     # Main state machine
├── config.h                 # User configuration
├── hardware_defs.h          # Pin definitions
├── app_state.h              # State definitions
├── utils/
│   ├── logger.h/cpp         # Debug logging
├── input/
│   ├── button_handler.h/cpp # Button debouncing
├── nfc/
│   ├── nfc_manager.h/cpp    # NFC interface
├── audio/
│   ├── audio_manager.h/cpp  # I2S audio
└── lte/
    └── lte_manager.h/cpp    # LTE modem
```

### Memory Usage
- Audio buffers: 32 KB (configurable)
- DMA buffers: ~4 KB (I2S driver)
- HTTP buffers: 16 KB
- Stack: ~8 KB
- **Total: ~60 KB** (out of 520 KB available)

### Power Budget
- ESP32: ~80 mA typical, 240 mA peak
- LTE modem: 50 mA idle, 800 mA TX peak
- MAX98357A: up to 500 mA peak (3W @ 4Ω)
- **Total: ~1.5A peak** (within USB 2.0 spec)

## Development Notes

### Non-Blocking Architecture
- State machine runs in `loop()` with no `delay()` calls
- All timeouts use `millis()` for timing
- Subsystems have `update()` functions called each iteration
- I2S operations use DMA (non-blocking)

### I2S Configuration Details
- Microphone and amplifier share BCLK and LRCLK
- I2S must be reconfigured when switching modes
- SPH0645 outputs 32-bit samples (converted to 16-bit)
- MAX98357A expects 16-bit samples
- Reconfiguration sequence:
  1. `i2s_driver_uninstall()`
  2. `i2s_driver_install()` with new config
  3. `i2s_set_pin()` with new pin config

### Error Handling
- All operations have timeouts
- HTTP operations retry up to 3 times
- Errors return to IDLE state (recoverable)
- FATAL errors enter ERROR state (requires reset)

### AT Command Reference

Power on modem:
```
digitalWrite(PWRKEY, LOW);
delay(1500);
digitalWrite(PWRKEY, HIGH);
```

Check network:
```
AT+CPIN?     → +CPIN: READY
AT+CREG?     → +CREG: 0,1 (registered)
```

HTTP GET:
```
AT+HTTPINIT
AT+HTTPPARA="CID",1
AT+HTTPPARA="URL","http://example.com/audio?uid=ABCD"
AT+HTTPACTION=0
AT+HTTPREAD
AT+HTTPTERM
```

HTTP POST:
```
AT+HTTPINIT
AT+HTTPPARA="CID",1
AT+HTTPPARA="URL","http://example.com/upload?uid=ABCD"
AT+HTTPPARA="CONTENT","application/octet-stream"
AT+HTTPDATA=32000,10000
<send binary data>
AT+HTTPACTION=1
AT+HTTPREAD
AT+HTTPTERM
```

## License

This is prototype firmware for development purposes only.

## Safety Notes

⚠️ **IMPORTANT**:
- Do NOT power LTE modem from ESP32 3.3V pin (current too high)
- LTE modem must be powered from USB 5V rail
- Ensure all grounds are connected together
- Do NOT use GPIO 0, 2, or 12 (boot mode pins)
- Maximum recording duration is limited to prevent memory overflow
- Device is prototype only - not for production use

## Support

For issues and questions:
1. Check Serial Monitor output at 115200 baud
2. Enable debug logging: Set log level to LOG_DEBUG
3. Verify all hardware connections
4. Test each subsystem individually following testing sequence
5. Check that backend API is correctly implemented

## Version History

**v1.0** - Initial implementation
- Complete state machine
- NFC UID reading
- LTE HTTP GET/POST
- I2S audio recording and playback
- Button input handling
- Comprehensive logging
