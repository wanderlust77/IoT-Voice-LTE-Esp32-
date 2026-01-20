# ESP32 Voice LTE - Testing Guide

This guide provides step-by-step testing procedures for each hardware subsystem. Test incrementally to isolate issues.

## Testing Philosophy

1. **Test bottom-up**: Start with basic hardware, work up to full system
2. **One subsystem at a time**: Isolate failures
3. **Log everything**: Use Serial Monitor at 115200 baud
4. **Document results**: Note what works and what doesn't

## Test Tools Required

- USB cable for ESP32
- Serial Monitor (Arduino IDE or PuTTY)
- NFC card (MIFARE Classic or compatible)
- Multimeter (for voltage checks)
- Oscilloscope (optional, for I2S debugging)
- Speaker (for audio testing)

---

## Phase 1: Basic ESP32 Hardware

### Test 1.1: Programming and Serial Output

**Purpose**: Verify ESP32 is programmable and Serial communication works

**Test Code**:
```cpp
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 Test - Hello World!");
}

void loop() {
  Serial.println("Loop running...");
  delay(1000);
}
```

**Expected Output**:
```
ESP32 Test - Hello World!
Loop running...
Loop running...
```

**If Failed**:
- Check USB cable
- Verify correct COM port selected
- Try different baud rate (9600)
- Press BOOT button during upload

---

### Test 1.2: GPIO Test (Blink)

**Purpose**: Verify GPIO functionality

**Test Code**:
```cpp
void setup() {
  pinMode(2, OUTPUT);  // Built-in LED
}

void loop() {
  digitalWrite(2, HIGH);
  delay(500);
  digitalWrite(2, LOW);
  delay(500);
}
```

**Expected Result**: Onboard LED blinks every 500ms

**If Failed**:
- LED pin may vary by board (try GPIO 2, 5, or 16)
- Check board power

---

## Phase 2: I2C NFC Reader (PN532)

### Test 2.1: I2C Bus Scan

**Purpose**: Verify PN532 is detected on I2C bus

**Test Code**:
```cpp
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 19);  // SDA=21, SCL=19
  Serial.println("I2C Scanner");
}

void loop() {
  Serial.println("Scanning...");
  int devices = 0;
  
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("Device found at 0x");
      Serial.println(addr, HEX);
      devices++;
    }
  }
  
  if (devices == 0) {
    Serial.println("No I2C devices found");
  }
  
  delay(5000);
}
```

**Expected Output**:
```
Scanning...
Device found at 0x24
```

**If Failed**:
- Check wiring: SDA→21, SCL→19, GND, VCC(3.3V)
- Verify PN532 is in I2C mode (check DIP switches)
- Try different I2C address (0x48)
- Check pull-up resistors (may be needed on SDA/SCL)

---

### Test 2.2: PN532 Firmware Version

**Purpose**: Verify PN532 communication

**Test Code**:
```cpp
#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ   27
#define PN532_RESET 14

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

void setup() {
  Serial.begin(115200);
  Serial.println("PN532 Test");
  
  Wire.begin(21, 19);  // SDA=21, SCL=19
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found!");
    while(1);
  }
  
  Serial.print("Found PN532 chip, firmware v");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);
  
  nfc.SAMConfig();
  Serial.println("Ready to read NFC cards...");
}

void loop() {
  uint8_t uid[7];
  uint8_t uidLength;
  
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
    Serial.print("UID: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    delay(1000);
  }
}
```

**Expected Output**:
```
Found PN532 chip, firmware v1.6
Ready to read NFC cards...
(Place card)
UID: AB CD 12 34
```

**If Failed**:
- Check IRQ pin (GPIO27)
- Check RESET pin (GPIO32)
- Verify card is MIFARE compatible
- Hold card closer (< 5cm)

---

## Phase 3: LTE Modem UART

### Test 3.1: UART Echo Test

**Purpose**: Verify UART communication with modem

**Test Code**:
```cpp
#define LTE_TX 17  // ESP32 TX -> Modem RX
#define LTE_RX 16  // ESP32 RX <- Modem TX

HardwareSerial LTESerial(2);

void setup() {
  Serial.begin(115200);
  LTESerial.begin(115200, SERIAL_8N1, LTE_RX, LTE_TX);
  Serial.println("LTE UART Test");
  Serial.println("Type AT commands in Serial Monitor");
}

void loop() {
  // Forward Serial to LTE
  if (Serial.available()) {
    char c = Serial.read();
    LTESerial.write(c);
  }
  
  // Forward LTE to Serial
  if (LTESerial.available()) {
    char c = LTESerial.read();
    Serial.write(c);
  }
}
```

**Manual Test**:
1. Open Serial Monitor
2. Type: `AT` and press Enter
3. Expected response: `OK`

**If Failed**:
- Check wiring: ESP TX(17)→Modem RX, ESP RX(16)→Modem TX
- Check baud rate (try 9600, 115200)
- Verify modem power (5V from USB, not ESP32!)
- Check ground connection

---

### Test 3.2: LTE Modem Power On

**Purpose**: Power on modem and verify AT commands

**Test Code**:
```cpp
#define LTE_TX 17
#define LTE_RX 16
#define LTE_PWRKEY 18

HardwareSerial LTESerial(2);

void setup() {
  Serial.begin(115200);
  LTESerial.begin(115200, SERIAL_8N1, LTE_RX, LTE_TX);
  
  pinMode(LTE_PWRKEY, OUTPUT);
  digitalWrite(LTE_PWRKEY, HIGH);
  
  Serial.println("Powering on LTE modem...");
  digitalWrite(LTE_PWRKEY, LOW);
  delay(1500);
  digitalWrite(LTE_PWRKEY, HIGH);
  
  Serial.println("Waiting for boot...");
  delay(5000);
  
  Serial.println("Testing AT commands...");
}

void loop() {
  static unsigned long lastTest = 0;
  
  if (millis() - lastTest > 5000) {
    lastTest = millis();
    
    Serial.println("Sending: AT");
    LTESerial.println("AT");
    delay(1000);
    
    while (LTESerial.available()) {
      Serial.write(LTESerial.read());
    }
  }
}
```

**Expected Output**:
```
Powering on LTE modem...
Waiting for boot...
Testing AT commands...
Sending: AT
AT
OK
```

**If Failed**:
- Verify PWRKEY pulse (LOW for 1.5s)
- Check modem power LED (if available)
- Increase boot wait time (try 10s)
- Check SIM card is inserted

---

### Test 3.3: Network Registration

**Purpose**: Verify modem can register on network

**Test Code**:
```cpp
// Use LTE manager from main firmware
#include "lte/lte_manager.h"
#include "hardware_defs.h"
#include "config.h"

LTEManager lte;

void setup() {
  Serial.begin(115200);
  Serial.println("Network Registration Test");
  
  if (!lte.init(PIN_LTE_TX, PIN_LTE_RX, PIN_LTE_PWRKEY, PIN_LTE_RESET, LTE_BAUD_RATE)) {
    Serial.println("LTE init failed!");
    return;
  }
  
  if (!lte.powerOn()) {
    Serial.println("Power on failed!");
    return;
  }
  
  Serial.println("Checking network (30s timeout)...");
  if (lte.checkNetwork(30000)) {
    Serial.println("Network registered!");
  } else {
    Serial.println("Network registration failed");
  }
}

void loop() {
  lte.update();
  delay(100);
}
```

**Expected Output**:
```
Network Registration Test
LTE init OK
Modem powered on
Checking network (30s timeout)...
Network registered!
```

**If Failed**:
- Check SIM card status (AT+CPIN?)
- Verify APN configuration
- Check cellular signal (try near window)
- Wait longer (up to 60s)
- Check SIM has active data plan

---

## Phase 4: I2S Audio Playback

### Test 4.1: I2S Sine Wave (Simple Test)

**Purpose**: Verify I2S TX configuration and amplifier

**Test Code**:
```cpp
#include <driver/i2s.h>

#define I2S_BCLK  26
#define I2S_LRC   25
#define I2S_DOUT  22
#define SAMPLE_RATE 16000

void setup() {
  Serial.begin(115200);
  Serial.println("I2S Playback Test");
  
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  
  Serial.println("Playing 1kHz tone for 3 seconds...");
  
  // Generate 1kHz sine wave
  int16_t buffer[512];
  for (int i = 0; i < 512; i++) {
    float t = (float)i / SAMPLE_RATE;
    buffer[i] = (int16_t)(sin(2.0 * PI * 1000.0 * t) * 10000);
  }
  
  // Play for 3 seconds
  for (int j = 0; j < (SAMPLE_RATE * 3 / 512); j++) {
    size_t written;
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
  }
  
  Serial.println("Done");
}

void loop() {
  delay(1000);
}
```

**Expected Result**: 
- Should hear 1kHz tone from speaker for 3 seconds
- Use oscilloscope to verify I2S signals if no audio

**If Failed**:
- Check I2S wiring: BCLK, LRCLK, DATA
- Verify MAX98357A power (5V or 3.3V)
- Check speaker connection to amplifier
- Verify amplifier enable/shutdown pins
- Check DMA buffer configuration

---

## Phase 5: I2S Audio Recording

### Test 5.1: Microphone Data Capture

**Purpose**: Verify I2S RX configuration and microphone

**Test Code**:
```cpp
#include <driver/i2s.h>

#define I2S_BCLK  26
#define I2S_LRC   25
#define I2S_DIN   33
#define SAMPLE_RATE 16000

void setup() {
  Serial.begin(115200);
  Serial.println("I2S Recording Test");
  
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // SPH0645 uses 32-bit
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN
  };
  
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  Serial.println("Recording for 2 seconds...");
  delay(1000);
  
  // Record and display samples
  int32_t buffer[512];
  int samples = 0;
  
  while (samples < SAMPLE_RATE * 2) {
    size_t bytesRead;
    i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
    
    // Print first 10 samples
    if (samples < 10) {
      Serial.print("Sample ");
      Serial.print(samples);
      Serial.print(": ");
      Serial.println(buffer[0]);
    }
    
    samples += bytesRead / 4;
  }
  
  Serial.println("Recording complete");
}

void loop() {
  delay(1000);
}
```

**Expected Output**:
```
I2S Recording Test
Recording for 2 seconds...
Sample 0: 1234567
Sample 1: 1245678
Sample 2: 1256789
...
Recording complete
```

**Check**: 
- Samples should be non-zero
- Values should change when you speak
- Silence should produce values near zero

**If Failed**:
- Check microphone wiring
- Verify microphone power (3.3V)
- Check SEL pin (GND for left channel)
- Use oscilloscope to verify I2S signals
- Try tapping microphone (should see spikes)

---

## Phase 6: Button Input

### Test 6.1: Button Debouncing

**Purpose**: Verify button hardware and debouncing

**Test Code**:
```cpp
#include "input/button_handler.h"
#include "hardware_defs.h"
#include "config.h"

ButtonHandler button;

void setup() {
  Serial.begin(115200);
  Serial.println("Button Test");
  
  button.init(PIN_BUTTON, LONG_PRESS_MS, DEBOUNCE_MS);
  Serial.println("Press button to test...");
}

void loop() {
  button.update();
  
  if (button.wasShortPress()) {
    Serial.println(">>> SHORT PRESS");
  }
  
  if (button.wasLongPress()) {
    Serial.println(">>> LONG PRESS");
  }
  
  if (button.isCurrentlyPressed()) {
    uint32_t duration = button.getCurrentPressDuration();
    if (duration % 100 < 10) {  // Print every ~100ms
      Serial.print("Holding: ");
      Serial.print(duration);
      Serial.println(" ms");
    }
  }
  
  delay(10);
}
```

**Expected Output**:
```
Button Test
Press button to test...
(Press briefly)
>>> SHORT PRESS
(Hold button)
Holding: 100 ms
Holding: 200 ms
...
Holding: 800 ms
>>> LONG PRESS
```

**If Failed**:
- Check button wiring to GPIO34
- **IMPORTANT**: GPIO34 is input-only and has NO internal pull-up
- You MUST use an external 10kΩ pull-up resistor to 3.3V
- Button should connect GPIO34 to GND when pressed
- Check debounce timing

---

## Phase 7: Integration Testing

### Test 7.1: Full Playback Flow

**Prerequisites**:
- Backend server running
- Audio file available at `/audio?uid=TESTUID`
- NFC card with known UID

**Test Procedure**:
1. Upload main firmware
2. Open Serial Monitor
3. Tap NFC card
4. Short press button
5. Observe logs and listen for audio

**Expected Log Flow**:
```
[...] [INFO] [Main] Short press detected -> PLAYBACK
[...] [INFO] [Main] State: IDLE -> READING_NFC
[...] [INFO] [NFC] UID read: ABCD1234 (4 bytes)
[...] [INFO] [Main] State: READING_NFC -> FETCH_AUDIO
[...] [INFO] [LTE] HTTP GET complete: 32000 bytes
[...] [INFO] [Main] State: FETCH_AUDIO -> PLAYING
[...] [INFO] [Audio] I2S configured for TX (playback)
[...] [INFO] [Main] Playback complete
[...] [INFO] [Main] State: PLAYING -> IDLE
```

---

### Test 7.2: Full Recording Flow

**Prerequisites**:
- Backend server running with `/upload` endpoint
- NFC card with known UID

**Test Procedure**:
1. Upload main firmware
2. Open Serial Monitor
3. Hold button
4. Tap NFC card (while holding)
5. Speak into microphone
6. Release button
7. Check server received audio

**Expected Log Flow**:
```
[...] [INFO] [Main] Long press detected -> RECORD
[...] [INFO] [Main] State: IDLE -> READING_NFC
[...] [INFO] [NFC] UID read: ABCD1234 (4 bytes)
[...] [INFO] [Main] State: READING_NFC -> RECORDING
[...] [INFO] [Audio] I2S configured for RX (recording)
[...] [INFO] [Main] Recording... (release button to stop)
(Release button)
[...] [INFO] [Main] Recording stopped: 32000 bytes, 2000 ms
[...] [INFO] [Main] State: RECORDING -> UPLOADING
[...] [INFO] [LTE] HTTP POST complete
[...] [INFO] [Main] State: UPLOADING -> IDLE
```

---

## Debugging Tips

### Enable Verbose Logging
In `utils/logger.cpp`, set:
```cpp
Logger::setLogLevel(LOG_DEBUG);
```

### Monitor Heap Memory
Add to main loop:
```cpp
if (millis() % 5000 < 10) {
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}
```

### Oscilloscope I2S Signals
- BCLK should be 512 kHz (16kHz * 32 bits/sample)
- LRCLK should be 16 kHz
- DATA should toggle on BCLK edges

### Multimeter Voltage Checks
- ESP32 3.3V rail: 3.3V ± 0.1V
- LTE modem power: 5.0V ± 0.2V
- Microphone power: 3.3V ± 0.1V

### Common Issues

**ESP32 won't program**: Hold BOOT button during upload

**I2C not working**: Add 4.7kΩ pull-ups on SDA and SCL

**Audio noise**: Add decoupling capacitors near power pins

**LTE won't connect**: Check SIM card, APN, and signal strength

**Memory errors**: Reduce buffer sizes in `config.h`

---

## Success Criteria

✅ **Phase 1**: LED blinks, serial output works  
✅ **Phase 2**: NFC UID read successfully  
✅ **Phase 3**: Modem responds to AT, network registered  
✅ **Phase 4**: Sine wave plays through speaker  
✅ **Phase 5**: Microphone captures non-zero data  
✅ **Phase 6**: Button detects short and long press  
✅ **Phase 7**: Full playback and recording flows work  

Once all phases pass, the system is fully functional!
