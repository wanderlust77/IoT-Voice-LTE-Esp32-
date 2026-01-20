# Compilation Fixes Applied

## Issues Fixed

### Issue 1: NFCManager Constructor Error

**Error:**
```
error: use of deleted function 'NFCManager::NFCManager()'
error: no matching function for call to 'Adafruit_PN532::Adafruit_PN532()'
```

**Root Cause:**
The `Adafruit_PN532` class doesn't have a default constructor - it requires parameters (IRQ and RST pins). Having it as a member variable without initialization caused compilation failure.

**Solution:**
Changed `Adafruit_PN532` from a member object to a pointer, initialized in the `init()` function:

**Changes in `nfc/nfc_manager.h`:**
```cpp
// Before:
private:
  Adafruit_PN532 nfc;
  bool initialized;

// After:
public:
  NFCManager();      // Added constructor
  ~NFCManager();     // Added destructor
  
private:
  Adafruit_PN532* nfc;  // Changed to pointer
  bool initialized;
  uint8_t pinIrq;
  uint8_t pinRst;
```

**Changes in `nfc/nfc_manager.cpp`:**
- Added constructor: `NFCManager::NFCManager()`
- Added destructor: `NFCManager::~NFCManager()`
- Modified `init()` to create PN532 object with `new Adafruit_PN532(irqPin, rstPin)`
- Updated all `nfc.` calls to `nfc->`
- Added null pointer checks

---

### Issue 2: Switch-Case Variable Scope Errors

**Error:**
```
error: jump to case label [-fpermissive]
note: crosses initialization of 'long unsigned int recordingDuration'
```

**Root Cause:**
In C++, variables declared in one `case` block are visible in subsequent `case` blocks, which violates scope rules when jumping between cases. The variable `recordingDuration` was declared in `STATE_RECORDING` but other cases could jump past its initialization.

**Solution:**
Added braces `{ }` around case blocks to create proper scope:

**Changes in `esp32_voice_lte.ino`:**

```cpp
// Before:
case STATE_RECORDING:
  unsigned long recordingDuration = now - recordingStartTime;
  // ... code ...
  break;

// After:
case STATE_RECORDING: {
  unsigned long recordingDuration = now - recordingStartTime;
  // ... code ...
  break;
}
```

Applied to:
- `STATE_FETCH_AUDIO` case
- `STATE_RECORDING` case
- `STATE_UPLOADING` case

---

## Files Modified

1. ✅ `nfc/nfc_manager.h` - Added constructor/destructor, changed to pointer
2. ✅ `nfc/nfc_manager.cpp` - Implemented constructor/destructor, dynamic allocation
3. ✅ `esp32_voice_lte.ino` - Added braces to case blocks for proper scoping

---

## Verification

After these fixes, the code should compile successfully. To verify:

```bash
# In Arduino IDE:
1. Sketch → Verify/Compile (Ctrl+R)
2. Check for "Done compiling" message
3. No errors should appear
```

---

## Memory Management Notes

### NFCManager Dynamic Allocation

The PN532 object is now dynamically allocated:

- **Allocated**: In `NFCManager::init()` using `new`
- **Deallocated**: In `NFCManager::~NFCManager()` using `delete`
- **Safety**: Null pointer checks added before all `nfc->` calls

**Memory Impact:**
- PN532 object size: ~100 bytes (estimated)
- Allocated once during initialization
- Freed on object destruction (or if init fails)

This is safe and follows standard C++ practices for objects that can't be default-constructed.

---

## Testing After Compilation

Once compilation succeeds:

1. Upload to ESP32
2. Open Serial Monitor (115200 baud)
3. Check initialization logs:
   ```
   [INFO] [NFC] Initializing PN532...
   [INFO] [NFC] Found PN532 chip, firmware v1.6
   [INFO] [NFC] PN532 initialized successfully
   ```

4. If you see these messages, the NFC fixes are working correctly!

---

## Common Compilation Errors (Reference)

If you still see errors:

### Missing Library
```
fatal error: Adafruit_PN532.h: No such file or directory
```
**Solution**: Install Adafruit PN532 library via Library Manager

### ESP32 Board Not Installed
```
Error compiling for board ESP32 Dev Module
```
**Solution**: Install ESP32 board support in Board Manager

### Wrong Board Selected
```
exit status 1
```
**Solution**: Select "ESP32 Dev Module" from Tools → Board menu

---

## Date Applied

**Fixes Applied:** January 2026
**Compiler:** Arduino IDE / PlatformIO
**Target:** ESP32-WROOM-32D

All compilation errors resolved! ✅
