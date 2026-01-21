# Fix: Microphone Outputting Constant 0x00000001 (SEL Connected to GND)

## Problem

**Symptoms:**
- SEL pin confirmed connected to GND ✅
- Raw I2S data: constant `0x00000001`
- All samples: constant `1` (not varying)
- Range: `1 to 1` (no variation)

## Root Cause Analysis

Since SEL is connected to GND, the constant value suggests:

### 1. **I2S Clock Signals Not Reaching Microphone** (Most Likely)

The SPH0645LM4H requires **BCLK** and **LRCLK** signals to output data. If these clocks aren't working, the microphone outputs a constant/invalid value.

**Check:**
- [ ] BCLK (GPIO 26) → SPH0645 BCLK (use oscilloscope or logic analyzer)
- [ ] LRCLK (GPIO 25) → SPH0645 LRCLK
- [ ] Both should show clock pulses when I2S is active

**Test:**
- Use oscilloscope/logic analyzer to verify clocks
- Or use a multimeter in frequency mode (should see ~512kHz for BCLK at 16kHz sample rate)

### 2. **I2S Communication Format Wrong**

SPH0645LM4H might need a different I2S format than `I2S_COMM_FORMAT_STAND_I2S`.

**Try:**
- `I2S_COMM_FORMAT_STAND_MSB`
- `I2S_COMM_FORMAT_I2S_MSB`
- Different bit alignment

### 3. **Microphone Not Receiving Power Properly**

**Check:**
- [ ] VDD = 3.3V (measure with multimeter)
- [ ] GND properly connected
- [ ] Power supply stable (no voltage drops)

### 4. **I2S Not Started/Initialized Correctly**

The I2S driver might not be generating clocks.

**Check:**
- I2S driver installed correctly
- Pins configured correctly
- No conflicts with other peripherals

---

## Diagnostic Steps

### Step 1: Verify I2S Clocks

**Hardware Check:**
1. Connect oscilloscope/logic analyzer to:
   - GPIO 26 (BCLK) - should see ~512kHz square wave
   - GPIO 25 (LRCLK) - should see ~16kHz square wave
2. If no clocks → I2S not configured correctly

**Software Check:**
Add this diagnostic code to verify I2S is running:
```cpp
// After starting recording
delay(1000);
size_t bytesAvailable = 0;
i2s_get_dma_buffer_info(I2S_PORT, NULL, &bytesAvailable);
Logger::printf(LOG_INFO, "Audio", "DMA buffer bytes available: %d", bytesAvailable);
```

### Step 2: Try Different I2S Formats

The firmware has been updated to try different formats. Check the logs for which format works.

### Step 3: Check Microphone Power

**Measure:**
- VDD pin voltage (should be 3.3V ± 0.1V)
- Current draw (should be a few mA)
- Power supply stability

### Step 4: Verify Wiring

**Double-check all connections:**
```
SPH0645    ESP32        Status
--------    -----        ------
VDD    →    3.3V        ✅ Check voltage
GND    →    GND         ✅ Check continuity
BCLK   →    GPIO 26     ⚠️  Check clock signal
LRCLK  →    GPIO 25     ⚠️  Check clock signal
DOUT   →    GPIO 33     ✅ Check connection
SEL    →    GND         ✅ Confirmed connected
```

---

## Solutions to Try

### Solution 1: Verify I2S Clocks (Most Important)

**If clocks aren't working:**
- Check I2S driver installation
- Verify pin configuration
- Check for pin conflicts
- Try different GPIO pins (if possible)

### Solution 2: Try Different I2S Format

The firmware will try different extraction methods. If raw values change but are still constant, try changing the I2S format in `audio_manager.cpp`:

```cpp
// Try this instead of STAND_I2S:
.communication_format = I2S_COMM_FORMAT_STAND_MSB,
```

### Solution 3: Check Microphone Model

**Verify you have SPH0645LM4H** (not a different model):
- Different models may have different I2S formats
- Check the part number on the microphone

### Solution 4: Microphone Initialization

Some microphones need a startup sequence. The SPH0645LM4H should start automatically, but try:
- Power cycle the microphone
- Wait longer after I2S start (current: 500ms, try 1000ms)

---

## Expected Behavior After Fix

**When working correctly:**
- Raw I2S values: **Varying** (e.g., `0x12345678`, `0x9ABCDEF0`, etc.)
- Sample values: **Varying** (e.g., `123, -456, 789, -234, ...`)
- Range: **Wide** (e.g., `-12345 to 23456`)
- Peak level: **> 0%** when speaking

---

## Most Likely Issue

**If SEL is connected to GND but still getting constant values:**

**90% chance:** I2S clocks (BCLK/LRCLK) are not reaching the microphone or not configured correctly.

**Next steps:**
1. ✅ Verify clocks with oscilloscope/logic analyzer
2. ✅ Check I2S driver is running
3. ✅ Try different I2S communication format
4. ✅ Verify microphone power (3.3V stable)

---

## Quick Test

**Add this to verify I2S is generating clocks:**
```cpp
// In setup(), after starting recording
pinMode(26, INPUT);
for (int i = 0; i < 100; i++) {
  Serial.print(digitalRead(26));  // Should toggle rapidly if BCLK is working
  delayMicroseconds(10);
}
Serial.println();
```

If you see all 0s or all 1s → Clocks not working.
If you see rapid toggling → Clocks working, issue is elsewhere.
