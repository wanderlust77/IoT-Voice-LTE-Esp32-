# Pinout Changes Applied - ESP32-WROOM-32D

## Summary of Changes

The following pin assignments have been updated throughout the entire codebase:

### ✅ Updated Pin Assignments

| Function | Old GPIO | New GPIO | Notes |
|----------|----------|----------|-------|
| **LTE PWRKEY** | GPIO 4 | **GPIO 18** | Avoids strapping pin |
| **LTE RESET** | GPIO 5 | **GPIO 23** | Avoids strapping pin |
| **NFC RST** | GPIO 14 | **GPIO 32** | Avoids strapping pin |
| **BUTTON** | GPIO 13 | **GPIO 34** | Avoids strapping pin, **INPUT-ONLY** |

### ✅ Unchanged Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| I2S BCLK | GPIO 26 | Shared by mic and amp |
| I2S LRCLK | GPIO 25 | Shared by mic and amp |
| I2S MIC DATA | GPIO 33 | SPH0645LM4H input |
| I2S AMP DATA | GPIO 22 | MAX98357A output |
| NFC SDA | GPIO 21 | I2C data |
| NFC SCL | GPIO 19 | I2C clock |
| NFC IRQ | GPIO 27 | Interrupt |
| LTE TX | GPIO 17 | UART2 to modem RX |
| LTE RX | GPIO 16 | UART2 from modem TX |

---

## ⚠️ CRITICAL HARDWARE REQUIREMENT

### External Pull-Up Resistor Required!

**GPIO 34 (Button) is an INPUT-ONLY pin** with **NO internal pull-up resistor**.

You **MUST** add an external pull-up resistor:

```
         3.3V
          |
         [10kΩ]  ← External resistor REQUIRED
          |
          ├────── GPIO 34
          |
       [Button]
          |
         GND
```

**Component:**
- 10kΩ resistor between GPIO 34 and 3.3V
- Button connects GPIO 34 to GND when pressed

**Without this resistor, the button will NOT work correctly!**

---

## Strapping Pins Avoided

The new pinout correctly avoids all ESP32 strapping pins:

**Strapping Pins (NOT USED):**
- GPIO 0, 2, 4, 5, 12, 13, 14, 15

These pins affect boot mode and should not be used for peripherals.

---

## Files Updated

All references to pin assignments have been updated in:

1. ✅ `hardware_defs.h` - Pin definitions
2. ✅ `input/button_handler.h` - Button handler header
3. ✅ `input/button_handler.cpp` - Button initialization (changed to INPUT mode)
4. ✅ `README.md` - Hardware wiring documentation
5. ✅ `TESTING_GUIDE.md` - All test code examples
6. ✅ `PROJECT_SUMMARY.md` - Hardware assembly notes

---

## Testing Impact

### Button Testing (Phase 6)

**Important Note:** The button test will fail if the external pull-up resistor is not installed.

Before testing, verify:
1. 10kΩ resistor connected between GPIO 34 and 3.3V
2. Button connects GPIO 34 to GND when pressed
3. Multimeter shows 3.3V on GPIO 34 when button is released
4. Multimeter shows 0V (GND) on GPIO 34 when button is pressed

---

## Hardware Checklist

Before powering on:

- [ ] I2S microphone connected to GPIO 26, 25, 33
- [ ] I2S amplifier connected to GPIO 26, 25, 22
- [ ] NFC module connected to GPIO 21, 19, 27, **32**
- [ ] LTE modem TX/RX connected to GPIO 17, 16
- [ ] LTE PWRKEY connected to **GPIO 18** (not GPIO 4)
- [ ] LTE RESET connected to **GPIO 23** (not GPIO 5)
- [ ] Button connected to **GPIO 34** (not GPIO 13)
- [ ] **10kΩ pull-up resistor from GPIO 34 to 3.3V installed**
- [ ] LTE modem powered from USB 5V (NOT ESP32 3.3V)
- [ ] All grounds connected together

---

## Verification Commands

After updating, verify pin definitions:

```bash
# Check hardware_defs.h
grep "define PIN_" hardware_defs.h
```

Expected output:
```
#define PIN_I2S_BCLK      26
#define PIN_I2S_LRCLK     25
#define PIN_I2S_MIC_DATA  33
#define PIN_I2S_AMP_DATA  22
#define PIN_NFC_SDA       21
#define PIN_NFC_SCL       19
#define PIN_NFC_IRQ       27
#define PIN_NFC_RST       32   ← UPDATED
#define PIN_LTE_TX        17
#define PIN_LTE_RX        16
#define PIN_LTE_PWRKEY    18   ← UPDATED
#define PIN_LTE_RESET     23   ← UPDATED
#define PIN_BUTTON        34   ← UPDATED
```

---

## Date Applied

**Changes Applied:** January 2026
**ESP32 Variant:** ESP32-WROOM-32D
**Firmware Version:** 1.0

All pinout changes have been applied and validated across the entire codebase.
