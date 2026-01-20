# LTE Modem Troubleshooting Guide

## Problem: Modem Not Responding to AT Commands

**Symptoms:**
- Debug output shows `TX: AT` but `RX:` is empty
- Error: "Failed to communicate with modem"

---

## Quick Diagnostic Checklist

### 1. Verify External 5V Power Supply
**CRITICAL:** The MIKROE-6287 LTE modem **requires external 5V power**. It cannot run on 3.3V from ESP32.

- [ ] **Check:** Is the modem's VCC connected to an external 5V power supply?
- [ ] **Check:** Is the GND of the 5V supply connected to ESP32 GND (common ground)?
- [ ] **Note:** USB power from ESP32 might not provide enough current for the modem

**Power LED:** The modem should have a power indicator LED. Is it lit?

---

### 2. Verify UART Wiring

Current configuration (from `hardware_defs.h`):
```
ESP32 GPIO 17 (TX) → Modem RX
ESP32 GPIO 16 (RX) ← Modem TX
```

**Common mistake:** TX/RX are often swapped. If modem doesn't respond:

- [ ] **Try swapping:** Connect ESP32 GPIO17 to Modem TX and GPIO16 to Modem RX
- [ ] **Check:** Are the wires properly seated in connectors?
- [ ] **Check:** Are you using the correct UART pins on the modem breakout board?

---

### 3. Check Modem Module Markings

The MIKROE-6287 uses different modem modules depending on version:
- **BG96** (Quectel) - Most common
- **BG95** or similar

**Check the actual chip on your board.** Different modules may have different:
- Default baud rates (try 9600, 115200, or auto-baud)
- Power-on sequences
- Boot times (some need 10-15 seconds)

---

### 4. Test with AT Commands Manually

**Use a USB-to-TTL adapter to test the modem directly:**

1. Disconnect modem from ESP32
2. Connect USB-to-TTL adapter:
   - Adapter TX → Modem RX
   - Adapter RX → Modem TX
   - Adapter GND → Modem GND
   - External 5V → Modem VCC

3. Open serial terminal (PuTTY, Arduino Serial Monitor, etc.) at **115200 baud**
4. Power on modem (pulse PWRKEY if needed)
5. Send `AT` and press Enter
6. **Expected response:** `OK`

If this doesn't work, the modem itself has an issue or needs different settings.

---

### 5. Verify PWRKEY Timing

The MIKROE-6287 requires a **power key pulse** to turn on:

**Current firmware does:**
1. PWRKEY LOW for 1.5 seconds
2. PWRKEY HIGH (release)
3. Wait 15 seconds for boot

**If modem was already on**, this pulse might turn it **OFF** instead.

**Try this:**
- Power cycle the entire system (ESP32 + Modem)
- Upload fresh firmware
- Observe if modem responds

---

### 6. Common Hardware Issues

| Issue | Check |
|-------|-------|
| **No 5V Power** | Measure voltage at modem VCC pin (should be ~5V) |
| **Insufficient Current** | LTE modem can draw 2A during transmission. Use beefy 5V supply |
| **TX/RX Swapped** | Swap GPIO16 and GPIO17 connections and retry |
| **Loose Wires** | Re-seat all connections, especially TX/RX/GND |
| **Wrong Ground** | Verify ESP32 GND and modem GND are connected together |
| **Modem in Sleep** | Some modems enter deep sleep. Try power cycling |

---

### 7. Try Different Baud Rates

Some modems auto-detect baud rate or default to 9600.

**In `config.h`, try:**
```cpp
#define LTE_BAUD_RATE  9600    // Instead of 115200
```

Or add auto-baud detection by trying multiple rates: 9600, 19200, 38400, 57600, 115200

---

### 8. Check Modem Status LEDs

Most LTE modem breakouts have status LEDs:
- **Power LED:** Solid = modem has power
- **Network LED:** Blinking = searching for network, Solid = connected
- **Status LED:** Various patterns indicate modem state

**What do your LEDs show?**

---

## Recommended Test Procedure

1. **Disconnect ESP32 from modem**
2. **Apply 5V power to modem only** (VCC + GND)
3. **Observe:** Does power LED turn on?
4. **Use USB-to-TTL adapter to test modem** (see step 4 above)
5. **If modem responds to adapter:** Wiring to ESP32 is the issue (check TX/RX)
6. **If modem doesn't respond to adapter:** Modem configuration or hardware issue

---

## Debug Output to Check

With the enhanced firmware, you should now see:
```
[INFO] [LTE] UART: RX=GPIO16, TX=GPIO17, Baud=115200
[INFO] [LTE] Checking if modem is already on...
[DEBUG] [LTE] TX: AT
[DEBUG] [LTE] No data received (timeout 1000ms)  ← This tells us nothing is coming back
```

If you see **"Received X bytes"** but still fails, the data is corrupted or wrong baud rate.

---

## Still Not Working?

**Share this info:**
1. What LEDs are lit on the modem board?
2. Measured voltage at modem VCC pin?
3. Did you try swapping TX/RX?
4. Does the modem work with a USB-to-TTL adapter?
5. What's printed on the actual modem chip? (BG96, BG95, SIM7000, etc.)
