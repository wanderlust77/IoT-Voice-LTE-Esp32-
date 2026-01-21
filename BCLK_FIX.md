# Fix: BCLK (GPIO 26) Stuck LOW

## Problem Identified

**Diagnostic Test Results:**
- ✅ **LRCLK (GPIO 25):** Working (toggling correctly)
- ❌ **BCLK (GPIO 26):** Stuck LOW (not working)

**Impact:**
Without BCLK, the microphone cannot output data correctly, resulting in constant `0x00000001` values.

---

## Fixes Applied

### 1. **Fixed I2S Communication Format**

**Before:**
```cpp
.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_I2S),
```

**After:**
```cpp
.communication_format = I2S_COMM_FORMAT_STAND_I2S,
```

The bitwise OR was incorrect and may have caused configuration issues.

### 2. **Added Explicit i2s_start() Call**

Added explicit `i2s_start()` after pin configuration to ensure I2S driver is fully started and clocks are generated.

---

## Additional Checks

### 1. **GPIO 26 Hardware Check**

**Verify:**
- [ ] GPIO 26 is not a strapping pin (it's not - GPIO 26 is safe)
- [ ] GPIO 26 is not used by another peripheral
- [ ] No short circuit on GPIO 26
- [ ] Wiring from GPIO 26 to SPH0645 BCLK is correct

**Test:**
- Measure voltage at GPIO 26 (should toggle between 0V and 3.3V if working)
- Check continuity from ESP32 GPIO 26 to microphone BCLK pin

### 2. **I2S Driver Status**

The firmware now logs:
```
[INFO] [Audio] I2S configured: 16000 Hz, mode=2
[INFO] [Audio] I2S pins: BCLK=GPIO26, LRCLK=GPIO25, DATA=GPIO33
```

If you see errors like `i2s_set_pin failed` or `i2s_start failed`, that's the issue.

### 3. **Try Different GPIO Pin (If Available)**

If GPIO 26 has a hardware issue, you could try a different pin, but this requires:
- Changing `PIN_I2S_BCLK` in `hardware_defs.h`
- Rewiring hardware
- Ensuring the new pin isn't a strapping pin

**Safe alternatives for BCLK:**
- GPIO 27 (but this is used for NFC IRQ)
- GPIO 14 (but this is a strapping pin - avoid)
- GPIO 4 (but this is a strapping pin - avoid)

**Current pinout constraints:**
- GPIO 0, 2, 4, 5, 12, 13, 14, 15 = Strapping pins (avoid)
- GPIO 16, 17 = LTE UART
- GPIO 18, 23 = LTE control
- GPIO 19, 21 = NFC I2C
- GPIO 22 = I2S AMP DATA
- GPIO 25 = I2S LRCLK (working)
- GPIO 26 = I2S BCLK (NOT working)
- GPIO 27 = NFC IRQ
- GPIO 32 = NFC RST
- GPIO 33 = I2S MIC DATA
- GPIO 34 = Button

**Possible alternative:** GPIO 35 (input-only, but BCLK is output, so can't use)

---

## Next Steps

1. **Upload the updated firmware** with the fixes
2. **Run the clock test again** (`esp32_i2s_clock_test.ino`)
3. **Check if BCLK is now working**

**Expected result after fix:**
```
BCLK Results: HIGH=95, LOW=105
>>> BCLK is toggling - clock appears to be WORKING! <<<
```

---

## If BCLK Still Doesn't Work

### Option 1: Check for Pin Conflicts

**Add this diagnostic code to check if GPIO 26 is being used elsewhere:**
```cpp
// In setup(), before initializing audio
pinMode(26, INPUT);
Serial.print("GPIO 26 initial state: ");
Serial.println(digitalRead(26));
```

### Option 2: Hardware Issue

If BCLK still doesn't work after software fixes:
- **GPIO 26 might be damaged** on the ESP32
- **Wiring issue** - check connection from ESP32 GPIO 26 to microphone BCLK
- **Microphone BCLK pin issue** - try a different microphone if available

### Option 3: Use Different GPIO (Last Resort)

If GPIO 26 is confirmed broken, you'd need to:
1. Change `PIN_I2S_BCLK` to a different GPIO
2. Rewire hardware
3. Ensure no conflicts

---

## Most Likely Solution

The **communication format fix** and **explicit i2s_start()** should resolve the issue. The incorrect bitwise OR in the communication format was likely preventing BCLK from being generated correctly.

**Upload the updated firmware and test again!**
