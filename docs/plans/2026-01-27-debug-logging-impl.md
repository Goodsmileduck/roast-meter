# Debug Logging Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add always-on debug logging that records every measurement to flash for beta period research.

**Architecture:** LittleFS partition stores binary ring buffer. RAM buffers 10 entries before flush. Serial commands dump as CSV.

**Tech Stack:** ESP32 LittleFS, PlatformIO custom partitions, C++ structs for binary format.

**Reference:** See `docs/plans/2026-01-27-debug-logging-design.md` for full design rationale.

---

## Task 1: Create Custom Partition Tables

**Files:**
- Create: `partitions_4mb_log.csv` (for S2 Mini, C3)

**Step 1: Create partition table with 1MB log partition**

Create file `partitions_4mb_log.csv`:

```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
logdata,  data, spiffs,  0x290000,0x100000,
spiffs,   data, spiffs,  0x390000,0x70000,
```

This allocates:
- 1.25MB per app slot (plenty for firmware)
- 1MB for logdata
- 448KB for general spiffs (if needed)

**Step 2: Update platformio.ini to use custom partitions**

Modify `platformio.ini`, add to `[common]` section:

```ini
board_build.partitions = partitions_4mb_log.csv
```

**Step 3: Build to verify partition table is valid**

Run: `pio run -e lolin_s2_mini`

Expected: Build succeeds without partition errors.

**Step 4: Commit**

```bash
git add partitions_4mb_log.csv platformio.ini
git commit -m "feat: add custom partition table with 1MB log partition"
```

---

## Task 2: Add Debug Logging Header and Structs

**Files:**
- Modify: `src/roast_meter.cpp` (add includes, defines, structs at top)

**Step 1: Add LittleFS include and compile-time toggles**

After line 8 (`#include "MAX30105.h"`), add:

```cpp
#include <LittleFS.h>

// -- Debug Logging Configuration --
#ifndef DEBUG_LOGGING_ENABLED
#define DEBUG_LOGGING_ENABLED 1
#endif
#ifndef DEBUG_SERIAL_COMMANDS
#define DEBUG_SERIAL_COMMANDS 1
#endif
```

**Step 2: Add log constants after preferences constants (after line 56)**

```cpp
// -- Debug Log constants --
#if DEBUG_LOGGING_ENABLED
#define LOG_FILE_PATH "/log.bin"
#define LOG_MAGIC 0x524F5354  // "ROST"
#define LOG_VERSION 1
#define LOG_MAX_ENTRIES 65000
#define LOG_BUFFER_SIZE 10
#define LOG_FLUSH_IDLE_MS 2000

// Log header structure (32 bytes)
struct LogHeader {
    uint32_t magic;           // 0x524F5354 "ROST"
    uint16_t version;         // Log format version
    uint16_t reserved1;       // Padding
    uint32_t writePosition;   // Next write index (0 to LOG_MAX_ENTRIES-1)
    uint32_t entryCount;      // Total entries written (can exceed MAX if wrapped)
    uint8_t  wrapped;         // 1 if buffer has wrapped
    uint8_t  reserved2[11];   // Padding to 32 bytes
};

// Log entry structure (16 bytes)
struct LogEntry {
    uint32_t timestamp;       // millis() value
    uint32_t rawIR;           // Raw IR sensor value
    int16_t  agtron;          // Calculated Agtron level
    uint8_t  ledBrightness;   // LED brightness setting
    uint8_t  intersectPt;     // Intersection point setting
    uint16_t deviationX1000;  // Deviation * 1000
    uint16_t flags;           // Reserved for future use
};
#endif
// -- End Debug Log constants --
```

**Step 3: Build to verify structs compile**

Run: `pio run -e lolin_s2_mini`

Expected: Build succeeds.

**Step 4: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: add debug logging structs and compile-time toggles"
```

---

## Task 3: Add Global Variables and Function Headers

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add debug log global variables after line 72 (after `bool oledAvailable`)**

```cpp
#if DEBUG_LOGGING_ENABLED
// Debug logging state
LogHeader logHeader;
LogEntry logBuffer[LOG_BUFFER_SIZE];
uint8_t logBufferCount = 0;
unsigned long lastLogFlush = 0;
unsigned long lastMeasurementTime = 0;
bool logFileOpen = false;
#endif
```

**Step 2: Add function headers after line 102 (after `void displayMeasurement(int rLevel);`)**

```cpp
#if DEBUG_LOGGING_ENABLED
void setupDebugLog();
void logMeasurement(uint32_t timestamp, uint32_t rawIR, int16_t agtron);
void flushLogBuffer();
#endif

#if DEBUG_SERIAL_COMMANDS
void handleSerialCommands();
void dumpLogToSerial();
void clearLog();
void printLogStatus();
#endif
```

**Step 3: Build to verify**

Run: `pio run -e lolin_s2_mini`

Expected: Build succeeds (functions declared but not defined yet - linker won't complain until called).

**Step 4: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: add debug logging globals and function declarations"
```

---

## Task 4: Implement setupDebugLog()

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add setupDebugLog implementation at end of file (before `// -- End Utility Functions --`)**

```cpp
// -- Debug Logging Functions --

#if DEBUG_LOGGING_ENABLED
void setupDebugLog() {
    Serial.println(F("Initializing debug log..."));

    if (!LittleFS.begin(true)) {  // true = format if mount fails
        Serial.println(F("ERROR: LittleFS mount failed"));
        return;
    }
    Serial.println(F("LittleFS mounted"));

    File file = LittleFS.open(LOG_FILE_PATH, "r+");
    if (!file) {
        // File doesn't exist, create it
        Serial.println(F("Creating new log file..."));
        file = LittleFS.open(LOG_FILE_PATH, "w+");
        if (!file) {
            Serial.println(F("ERROR: Cannot create log file"));
            return;
        }

        // Initialize header
        memset(&logHeader, 0, sizeof(LogHeader));
        logHeader.magic = LOG_MAGIC;
        logHeader.version = LOG_VERSION;
        logHeader.writePosition = 0;
        logHeader.entryCount = 0;
        logHeader.wrapped = 0;

        file.write((uint8_t*)&logHeader, sizeof(LogHeader));
        file.close();

        Serial.println(F("Log file created"));
    } else {
        // Read existing header
        file.read((uint8_t*)&logHeader, sizeof(LogHeader));
        file.close();

        // Validate header
        if (logHeader.magic != LOG_MAGIC || logHeader.version != LOG_VERSION) {
            Serial.println(F("WARNING: Log corrupted, reinitializing..."));
            LittleFS.remove(LOG_FILE_PATH);
            setupDebugLog();  // Recursive call to create fresh
            return;
        }

        Serial.printf("Log loaded: %lu entries, wrapped=%d\n",
                      logHeader.entryCount, logHeader.wrapped);
    }

    logFileOpen = true;
    logBufferCount = 0;
    lastLogFlush = millis();
}
#endif

// -- End Debug Logging Functions --
```

**Step 2: Call setupDebugLog from setup() after setupPreferences()**

In `setup()` function, after line 139 (`setupPreferences();`), add:

```cpp
#if DEBUG_LOGGING_ENABLED
    setupDebugLog();
#endif
```

**Step 3: Build and upload to test**

Run: `pio run -e lolin_s2_mini -t upload && pio device monitor`

Expected: Serial output shows "Initializing debug log...", "LittleFS mounted", "Log file created" (first run) or "Log loaded: X entries" (subsequent runs).

**Step 4: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: implement setupDebugLog with corruption recovery"
```

---

## Task 5: Implement logMeasurement() and flushLogBuffer()

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add logMeasurement and flushLogBuffer after setupDebugLog()**

```cpp
#if DEBUG_LOGGING_ENABLED
void logMeasurement(uint32_t timestamp, uint32_t rawIR, int16_t agtron) {
    if (!logFileOpen) return;

    // Create entry
    LogEntry entry;
    entry.timestamp = timestamp;
    entry.rawIR = rawIR;
    entry.agtron = agtron;
    entry.ledBrightness = ledBrightness;
    entry.intersectPt = (uint8_t)intersectionPoint;
    entry.deviationX1000 = (uint16_t)(deviation * 1000);
    entry.flags = 0;

    // Add to buffer
    logBuffer[logBufferCount++] = entry;
    lastMeasurementTime = millis();

    // Flush if buffer full
    if (logBufferCount >= LOG_BUFFER_SIZE) {
        flushLogBuffer();
    }
}

void flushLogBuffer() {
    if (!logFileOpen || logBufferCount == 0) return;

    File file = LittleFS.open(LOG_FILE_PATH, "r+");
    if (!file) {
        Serial.println(F("ERROR: Cannot open log for write"));
        return;
    }

    // Write each buffered entry
    for (uint8_t i = 0; i < logBufferCount; i++) {
        // Calculate file position: header + (writePosition * entrySize)
        uint32_t pos = sizeof(LogHeader) + (logHeader.writePosition * sizeof(LogEntry));
        file.seek(pos);
        file.write((uint8_t*)&logBuffer[i], sizeof(LogEntry));

        // Update write position (circular)
        logHeader.writePosition++;
        if (logHeader.writePosition >= LOG_MAX_ENTRIES) {
            logHeader.writePosition = 0;
            logHeader.wrapped = 1;
        }
        logHeader.entryCount++;
    }

    // Update header
    file.seek(0);
    file.write((uint8_t*)&logHeader, sizeof(LogHeader));
    file.close();

    logBufferCount = 0;
    lastLogFlush = millis();
}
#endif
```

**Step 2: Call logMeasurement in measureSampleJob() after displayMeasurement()**

In `measureSampleJob()`, after line 358 (`displayMeasurement(calibratedAgtronLevel);`), add:

```cpp
#if DEBUG_LOGGING_ENABLED
            logMeasurement(millis(), rLevel, calibratedAgtronLevel);
#endif
```

**Step 3: Add idle flush check at start of measureSampleJob()**

At the beginning of `measureSampleJob()` function, after line 322, add:

```cpp
#if DEBUG_LOGGING_ENABLED
    // Flush log buffer if idle
    if (logBufferCount > 0 && (millis() - lastMeasurementTime > LOG_FLUSH_IDLE_MS)) {
        flushLogBuffer();
    }
#endif
```

**Step 4: Build and upload to test**

Run: `pio run -e lolin_s2_mini -t upload && pio device monitor`

Expected: Normal operation. Place sample on sensor, see Agtron readings. Serial shows no errors.

**Step 5: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: implement logMeasurement with RAM buffering and idle flush"
```

---

## Task 6: Implement Serial Commands Parser

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add handleSerialCommands() implementation**

```cpp
#if DEBUG_SERIAL_COMMANDS
void handleSerialCommands() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "LOG DUMP") {
        dumpLogToSerial();
    } else if (cmd == "LOG CLEAR") {
        clearLog();
    } else if (cmd == "LOG STATUS") {
        printLogStatus();
    }
}
#endif
```

**Step 2: Call handleSerialCommands() at start of loop()**

In `loop()` function, before `measureSampleJob();`, add:

```cpp
#if DEBUG_SERIAL_COMMANDS
    handleSerialCommands();
#endif
```

**Step 3: Build to verify**

Run: `pio run -e lolin_s2_mini`

Expected: Build succeeds (dumpLogToSerial, clearLog, printLogStatus not implemented yet but not called in this build path until we complete them).

**Step 4: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: add serial command parser for LOG commands"
```

---

## Task 7: Implement LOG STATUS Command

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add printLogStatus() implementation**

```cpp
#if DEBUG_SERIAL_COMMANDS
void printLogStatus() {
#if DEBUG_LOGGING_ENABLED
    if (!logFileOpen) {
        Serial.println(F("LOG STATUS: Logging not initialized"));
        return;
    }

    Serial.println(F("=== ROAST METER LOG STATUS ==="));
    Serial.printf("Entries logged: %lu\n", logHeader.entryCount);
    Serial.printf("Current position: %lu / %d\n", logHeader.writePosition, LOG_MAX_ENTRIES);
    Serial.printf("Wrapped: %s\n", logHeader.wrapped ? "YES" : "NO");
    Serial.printf("Buffer pending: %d\n", logBufferCount);

    uint32_t usedEntries = logHeader.wrapped ? LOG_MAX_ENTRIES : logHeader.writePosition;
    float capacityPct = (usedEntries * 100.0f) / LOG_MAX_ENTRIES;
    Serial.printf("Capacity used: %.1f%%\n", capacityPct);
#else
    Serial.println(F("LOG STATUS: Logging disabled at compile time"));
#endif
}
#endif
```

**Step 2: Build and upload to test**

Run: `pio run -e lolin_s2_mini -t upload && pio device monitor`

Expected: After warmup, type `LOG STATUS` in serial monitor. Should print status info.

**Step 3: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: implement LOG STATUS command"
```

---

## Task 8: Implement LOG CLEAR Command

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add clearLog() implementation**

```cpp
#if DEBUG_SERIAL_COMMANDS
void clearLog() {
#if DEBUG_LOGGING_ENABLED
    if (!logFileOpen) {
        Serial.println(F("LOG CLEAR: Logging not initialized"));
        return;
    }

    // Reset header
    logHeader.writePosition = 0;
    logHeader.entryCount = 0;
    logHeader.wrapped = 0;

    // Write header to file
    File file = LittleFS.open(LOG_FILE_PATH, "r+");
    if (file) {
        file.seek(0);
        file.write((uint8_t*)&logHeader, sizeof(LogHeader));
        file.close();
    }

    // Clear buffer
    logBufferCount = 0;

    Serial.println(F("LOG CLEAR: Log cleared successfully"));
#else
    Serial.println(F("LOG CLEAR: Logging disabled at compile time"));
#endif
}
#endif
```

**Step 2: Build and upload to test**

Run: `pio run -e lolin_s2_mini -t upload && pio device monitor`

Expected: Type `LOG CLEAR`, then `LOG STATUS`. Should show 0 entries.

**Step 3: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: implement LOG CLEAR command"
```

---

## Task 9: Implement LOG DUMP Command

**Files:**
- Modify: `src/roast_meter.cpp`

**Step 1: Add dumpLogToSerial() implementation**

```cpp
#if DEBUG_SERIAL_COMMANDS
void dumpLogToSerial() {
#if DEBUG_LOGGING_ENABLED
    if (!logFileOpen) {
        Serial.println(F("LOG DUMP: Logging not initialized"));
        return;
    }

    // Flush any pending entries first
    flushLogBuffer();

    File file = LittleFS.open(LOG_FILE_PATH, "r");
    if (!file) {
        Serial.println(F("LOG DUMP: Cannot open log file"));
        return;
    }

    // Re-read header (in case it changed)
    file.read((uint8_t*)&logHeader, sizeof(LogHeader));

    uint32_t entriesToRead = logHeader.wrapped ? LOG_MAX_ENTRIES : logHeader.writePosition;
    uint32_t startPos = logHeader.wrapped ? logHeader.writePosition : 0;

    Serial.println(F("=== ROAST METER LOG DUMP ==="));
    Serial.printf("ENTRIES: %lu\n", entriesToRead);
    Serial.printf("WRAPPED: %s\n", logHeader.wrapped ? "YES" : "NO");
    Serial.println(F("--- BEGIN CSV ---"));
    Serial.println(F("timestamp_ms,raw_ir,agtron,led_brightness,intersection_point,deviation"));

    LogEntry entry;
    for (uint32_t i = 0; i < entriesToRead; i++) {
        uint32_t idx = (startPos + i) % LOG_MAX_ENTRIES;
        uint32_t pos = sizeof(LogHeader) + (idx * sizeof(LogEntry));
        file.seek(pos);
        file.read((uint8_t*)&entry, sizeof(LogEntry));

        Serial.printf("%lu,%lu,%d,%d,%d,%.3f\n",
                      entry.timestamp,
                      entry.rawIR,
                      entry.agtron,
                      entry.ledBrightness,
                      entry.intersectPt,
                      entry.deviationX1000 / 1000.0f);

        // Yield to prevent watchdog timeout on large dumps
        if (i % 100 == 0) {
            yield();
        }
    }

    Serial.println(F("--- END CSV ---"));
    file.close();
#else
    Serial.println(F("LOG DUMP: Logging disabled at compile time"));
#endif
}
#endif
```

**Step 2: Build and upload to test**

Run: `pio run -e lolin_s2_mini -t upload && pio device monitor`

Expected: Take some measurements, then type `LOG DUMP`. Should output CSV data with your measurements.

**Step 3: Commit**

```bash
git add src/roast_meter.cpp
git commit -m "feat: implement LOG DUMP command with CSV output"
```

---

## Task 10: Test on XIAO ESP32-C3

**Files:**
- None (testing only)

**Step 1: Build for XIAO C3**

Run: `pio run -e seeed_xiao_esp32c3`

Expected: Build succeeds with same partition table.

**Step 2: Upload and test (if hardware available)**

Run: `pio run -e seeed_xiao_esp32c3 -t upload && pio device monitor`

Test:
1. Verify warmup completes
2. Take measurements
3. `LOG STATUS` - shows entries
4. `LOG DUMP` - outputs CSV
5. `LOG CLEAR` - clears log
6. `LOG STATUS` - shows 0 entries

**Step 3: Commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix: adjustments for XIAO C3 compatibility"
```

---

## Task 11: Add Compile Toggle Build Variants (Optional)

**Files:**
- Modify: `platformio.ini`

**Step 1: Add production build variant without debug logging**

Add to `platformio.ini`:

```ini
[env:lolin_s2_mini_release]
extends = common
board = lolin_s2_mini
board_build.partitions = partitions_4mb_log.csv
build_flags =
    -D FIRMWARE_REVISION_STRING='"v0.3"'
    -D DEBUG_LOGGING_ENABLED=0
    -D DEBUG_SERIAL_COMMANDS=0

[env:seeed_xiao_esp32c3_release]
extends = common
board = seeed_xiao_esp32c3
board_build.partitions = partitions_4mb_log.csv
build_flags =
    -D FIRMWARE_REVISION_STRING='"v0.3"'
    -D I2C_SDA=6
    -D I2C_SCL=7
    -D DEBUG_LOGGING_ENABLED=0
    -D DEBUG_SERIAL_COMMANDS=0
```

**Step 2: Build release variant to verify it compiles without logging**

Run: `pio run -e lolin_s2_mini_release`

Expected: Build succeeds, firmware size slightly smaller.

**Step 3: Commit**

```bash
git add platformio.ini
git commit -m "feat: add release build variants with logging disabled"
```

---

## Task 12: Create Helper Script (Optional)

**Files:**
- Create: `tools/capture_log.py`

**Step 1: Create capture script**

```python
#!/usr/bin/env python3
"""
Roast Meter Log Capture Tool
Connects to device, sends LOG DUMP, saves CSV output.

Usage: python capture_log.py [port] [output.csv]
"""

import sys
import serial
import time

def capture_log(port='/dev/ttyUSB0', output='roast_log.csv', baud=115200):
    print(f"Connecting to {port}...")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(2)  # Wait for device

    # Clear any pending data
    ser.flushInput()

    print("Sending LOG DUMP command...")
    ser.write(b'LOG DUMP\n')

    lines = []
    in_csv = False

    print("Receiving data...")
    timeout_start = time.time()
    while time.time() - timeout_start < 300:  # 5 min max
        line = ser.readline().decode('utf-8', errors='ignore').strip()

        if '--- BEGIN CSV ---' in line:
            in_csv = True
            continue
        elif '--- END CSV ---' in line:
            break
        elif in_csv and line:
            lines.append(line)
            if len(lines) % 1000 == 0:
                print(f"  {len(lines)} entries...")

    ser.close()

    if lines:
        with open(output, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        print(f"Saved {len(lines)-1} entries to {output}")
    else:
        print("No data received")

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    output = sys.argv[2] if len(sys.argv) > 2 else 'roast_log.csv'
    capture_log(port, output)
```

**Step 2: Commit**

```bash
mkdir -p tools
git add tools/capture_log.py
git commit -m "feat: add log capture helper script"
```

---

## Task 13: Update Version and Final Commit

**Files:**
- Modify: `src/roast_meter.cpp`
- Modify: `platformio.ini`

**Step 1: Update version string**

In `src/roast_meter.cpp` line 1, change:

```cpp
// VERSION 0.3-beta
```

In `platformio.ini`, update all `FIRMWARE_REVISION_STRING` to `'"v0.3-beta"'`

**Step 2: Final build verification**

Run: `pio run`

Expected: All environments build successfully.

**Step 3: Commit**

```bash
git add -A
git commit -m "chore: bump version to 0.3-beta with debug logging"
```

---

## Summary

After completing all tasks:

- Custom partition table with 1MB log space
- Binary ring buffer logging (~65K entries)
- RAM buffering (10 entries) with idle flush
- Serial commands: `LOG DUMP`, `LOG CLEAR`, `LOG STATUS`
- CSV output directly from device
- Compile-time toggles for production builds
- Works on both LOLIN S2 Mini and XIAO ESP32-C3
