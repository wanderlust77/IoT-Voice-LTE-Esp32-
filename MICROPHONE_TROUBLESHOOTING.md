# SPH0645LM4H Microphone Troubleshooting

## Problem: Getting Only 0/1 Values

**Symptoms:**
- Audio samples alternating between 0 and 1
- Average absolute value = 0
- Peak level < 0.1%

---

## Hardware Checks

### 1. **SEL Pin (Channel Select) - CRITICAL!**

The SPH0645LM4H has a **SEL pin** that must be connected to **GND** for left channel operation.

**Check:**
- [ ] SEL pin is connected to **GND** (not floating, not VDD)
- [ ] This is required for the microphone to output audio data

**If SEL is floating or connected to VDD:**
- Microphone may output invalid data
- You'll see patterns like 0, 1, 0, 1... or constant values

---

### 2. **Power Supply**

**Check:**
- [ ] VDD connected to **3.3V** (not 5V!)
- [ ] GND connected to ESP32 GND
- [ ] Power is stable (check with multimeter)

**SPH0645LM4H is 3.3V only** - 5V will damage it!

---

### 3. **I2S Wiring**

**SPH0645LM4H → ESP32:**
```
SPH0645    ESP32
BCLK   →   GPIO 26
LRCLK  →   GPIO 25
DOUT   →   GPIO 33
SEL    →   GND (CRITICAL!)
VDD    →   3.3V
GND    →   GND
```

**Verify:**
- [ ] All connections are solid (no loose wires)
- [ ] No shorts between pins
- [ ] Correct GPIO pins (26, 25, 33)

---

### 4. **Microphone Orientation**

**Check:**
- [ ] Microphone sound port is facing the correct direction
- [ ] No obstruction blocking the microphone
- [ ] Microphone is not damaged

---

## Software Checks

### 1. **I2S Configuration**

The firmware is configured for:
- **32-bit samples** (SPH0645 outputs 32-bit words)
- **16 kHz sample rate**
- **Left channel only**

This should be correct. The extraction method has been updated to take upper 16 bits from 32-bit samples.

---

### 2. **Data Extraction**

The SPH0645LM4H outputs 32-bit words with 18-bit audio data. The current code extracts the upper 16 bits:
```cpp
audioData = (int32_t)((int16_t)(sample32 >> 16));
```

If this still gives 0/1 values, try the alternative method (uncomment in `audio_manager.cpp`):
```cpp
// Method 2: Lower 18 bits
audioData = (int32_t)((int16_t)(sample32 & 0x3FFFF) >> 2);
```

---

## Diagnostic Steps

### Step 1: Check Raw 32-bit Values

The firmware now logs the first raw 32-bit samples. Look for:
```
[INFO] [Audio] First raw 32-bit samples (hex): XXXXXXXX, XXXXXXXX, XXXXXXXX
```

**What to look for:**
- **All zeros (0x00000000)** → Microphone not powered or not connected
- **All same value (0x12345678 repeated)** → Microphone stuck or wrong configuration
- **Alternating pattern (0x00000000, 0x00000001)** → Wrong bit extraction or SEL pin issue
- **Varying values** → Good! Microphone is working, just need correct extraction

---

### Step 2: Verify SEL Pin

**Most common issue:** SEL pin not connected to GND

**Test:**
1. Disconnect power
2. Measure resistance between SEL pin and GND
3. Should be very low (< 1 ohm) if properly connected
4. If high resistance or open circuit → **This is your problem!**

---

### Step 3: Test with Known Good Signal

If possible, connect the microphone to a known working I2S setup to verify the microphone itself works.

---

## Expected Behavior After Fix

Once working correctly, you should see:
- **Sample values:** Varying between -32768 and +32768
- **Peak level:** > 1% when speaking
- **Average absolute value:** > 100 (when speaking)
- **Zero samples:** < 5% of total

---

## Quick Fix Checklist

1. ✅ **SEL pin connected to GND?** (Most likely issue!)
2. ✅ **VDD = 3.3V?** (Not 5V!)
3. ✅ **All I2S pins connected correctly?**
4. ✅ **Microphone powered on?**
5. ✅ **Speaking into microphone?** (Test with loud voice)

---

## If Still Not Working

**Share these diagnostics:**
1. Raw 32-bit hex values from first read
2. SEL pin connection status (GND or floating?)
3. Voltage at VDD pin (should be 3.3V)
4. Sample values you're seeing (first 20 samples)

The most common fix is **connecting SEL to GND**!
