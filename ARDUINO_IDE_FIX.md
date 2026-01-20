# Arduino IDE Compatibility Fix

## Issue: Subdirectory .cpp Files Not Compiled

**Problem:**
Arduino IDE does not automatically compile `.cpp` files in subdirectories of the sketch folder. This causes linker errors like:

```
undefined reference to `Logger::init(unsigned int)'
undefined reference to `ButtonHandler::update()'
undefined reference to `NFCManager::readUID(...)'
```

## Solution: Flat File Structure

All source files have been moved to the root sketch folder:

### Before (Subdirectories - Not Compatible)
```
esp32_voice_lte/
├── esp32_voice_lte.ino
├── utils/
│   ├── logger.h
│   └── logger.cpp          ❌ Not compiled by Arduino IDE
├── input/
│   ├── button_handler.h
│   └── button_handler.cpp  ❌ Not compiled
└── ...
```

### After (Flat - Compatible)
```
esp32_voice_lte/
├── esp32_voice_lte.ino
├── logger.h
├── logger.cpp              ✅ Compiled by Arduino IDE
├── button_handler.h
├── button_handler.cpp      ✅ Compiled
├── nfc_manager.h
├── nfc_manager.cpp         ✅ Compiled
├── audio_manager.h
├── audio_manager.cpp       ✅ Compiled
├── lte_manager.h
└── lte_manager.cpp         ✅ Compiled
```

## Changes Applied

1. **Moved all module files to root directory**
2. **Updated include paths** in all files:
   - `#include "utils/logger.h"` → `#include "logger.h"`
   - `#include "input/button_handler.h"` → `#include "button_handler.h"`
   - etc.

3. **Removed empty subdirectories**

## File Organization

Even though files are in one folder, they remain logically separated by naming:

| Module | Files |
|--------|-------|
| **Logger** | `logger.h`, `logger.cpp` |
| **Button** | `button_handler.h`, `button_handler.cpp` |
| **NFC** | `nfc_manager.h`, `nfc_manager.cpp` |
| **Audio** | `audio_manager.h`, `audio_manager.cpp` |
| **LTE** | `lte_manager.h`, `lte_manager.cpp` |
| **Main** | `esp32_voice_lte.ino` |
| **Config** | `config.h`, `hardware_defs.h`, `app_state.h` |

## Alternative Solutions (Not Used)

If you prefer subdirectories in the future:

**Option 1: Use src/ folder**
- Arduino IDE 1.8.10+ supports a `src/` subdirectory
- Move all .cpp/.h files to `esp32_voice_lte/src/`
- Arduino will compile everything in `src/`

**Option 2: Use PlatformIO**
- Full support for subdirectory structures
- Better build system than Arduino IDE
- More control over compiler flags

**Option 3: Create library**
- Package modules as Arduino library
- Install in Arduino/libraries folder
- Include via Library Manager

## Compilation Should Now Work

With files in the root folder, Arduino IDE will:

1. ✅ Compile all `.cpp` files automatically
2. ✅ Link all object files together
3. ✅ Generate working firmware

Try compiling again - the "undefined reference" errors should be gone!

## Verification

Check that Arduino IDE finds all files:

1. Open `esp32_voice_lte.ino` in Arduino IDE
2. Look at tabs at the top - you should see:
   - esp32_voice_lte.ino
   - config.h
   - hardware_defs.h
   - app_state.h
   - logger.h
   - button_handler.h
   - nfc_manager.h
   - audio_manager.h
   - lte_manager.h

3. Verify/Compile - should succeed!

---

**Date Applied:** January 2026  
**Issue:** Arduino IDE subdirectory compilation  
**Status:** RESOLVED ✅
