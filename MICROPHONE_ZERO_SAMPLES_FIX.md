# Fix: All Microphone Samples Are Zero

## Problem

**Symptoms:**
- All audio samples = 0
- Raw I2S data shows `0x00000001` (constant value)
- Warning: "All samples are ZERO - microphone not sending data!"

## Root Cause

The raw I2S value `0x00000001` indicates the **SPH0645LM4H microphone is stuck outputting a constant value** instead of real audio data. This is almost always caused by:

### **CRITICAL: SEL Pin Not Connected to GND**

The SPH0645LM4H has a **SEL (Select) pin** that **MUST be connected to GND** for the microphone to work properly.

**If SEL is:**
- **Floating (not connected)** → Microphone outputs invalid/constant data
- **Connected to VDD (3.3V)** → Microphone outputs invalid/constant data  
- **Connected to GND** → ✅ Microphone works correctly

---

## Hardware Fix

### Step 1: Check SEL Pin Connection

**SPH0645LM4H Pinout:**
```
Pin    Function    ESP32 Connection
---    --------    -----------------
VDD    Power       3.3V
GND    Ground      GND
BCLK   Bit Clock   GPIO 26
LRCLK  Word Clock  GPIO 25
DOUT   Data Out    GPIO 33
SEL    Channel     GND (CRITICAL!)
```

**Action Required:**
1. **Disconnect power** from ESP32
2. **Measure resistance** between SEL pin and GND
   - Should be **< 1 ohm** if properly connected
   - If **high resistance or open circuit** → **This is your problem!**
3. **Connect SEL pin to GND** with a wire or jumper
4. **Re-test**

---

## Verification

After connecting SEL to GND:

1. **Upload firmware** and check Serial Monitor
2. **Look for raw I2S values** - should see **varying hex values**, not constant `0x00000001`
3. **Sample values** should vary (not all 0)
4. **Peak level** should be > 0% when speaking

**Expected output after fix:**
```
[INFO] [Audio] Data read #1: samples=100, raw[0]=12345678, raw[1]=9ABCDEF0
[DATA] Read #1 - Samples [0..99]: 123, -456, 789, -234, ...
[INFO] [Main] Range: -12345 to 23456 (max: ±32768)
[INFO] [Main] Peak level: 71.6% - Audio looks good!
```

---

## Other Possible Issues (If SEL is Connected)

If SEL is connected to GND but still getting zeros:

### 1. **Power Supply**
- Check VDD = 3.3V (not 5V!)
- Measure voltage at microphone VDD pin
- Verify GND connection

### 2. **I2S Clock Signals**
- BCLK (GPIO 26) → Check with oscilloscope (should see clock pulses)
- LRCLK (GPIO 25) → Check with oscilloscope (should see word clock)
- If no clocks → Microphone can't output data

### 3. **Data Line**
- DOUT (GPIO 33) → Check connection
- Verify no shorts or loose wires

### 4. **Microphone Orientation**
- Sound port facing correct direction
- No obstruction blocking microphone

---

## Quick Diagnostic Test

**Test SEL pin connection:**
```cpp
// Add this to setup() temporarily
pinMode(33, INPUT);
for (int i = 0; i < 100; i++) {
  Serial.print(digitalRead(33));  // Should see varying values if mic working
  delay(10);
}
```

If you see all 0s or all 1s → SEL pin issue or microphone not working.

---

## Most Likely Solution

**99% of the time, the fix is:**
1. ✅ **Connect SEL pin to GND**
2. ✅ **Re-test**

The constant `0x00000001` value is a classic symptom of SEL pin not being connected to GND.

---

## After Fixing

Once SEL is connected to GND:
- Raw I2S values should vary (not constant)
- Sample values should vary (not all 0)
- Peak level should increase when speaking
- Audio recording should work correctly

**The microphone hardware is likely fine - it just needs SEL connected to GND!**
