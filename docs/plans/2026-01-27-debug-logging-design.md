# Debug Logging Feature Design

**Date:** 2026-01-27
**Status:** Draft
**Goal:** Comprehensive data logging for beta period research

## Overview

Add a debug logging system that records every measurement to flash storage for later retrieval via serial commands. This enables:

- Diagnosing calibration issues from user data
- Detecting hardware/LED drift over time
- Collecting real-world data to improve the Agtron conversion algorithm

## Design Decisions

| Aspect | Decision |
|--------|----------|
| Storage | 1MB LittleFS partition, binary ring buffer |
| Logging mode | Always on, every measurement |
| Data fields | timestamp_ms, raw_ir, agtron, led_brightness, intersection_point, deviation |
| Entry size | 16 bytes binary (~65K entries capacity) |
| When full | Circular buffer, overwrite oldest |
| Retrieval | Serial command `LOG DUMP` outputs CSV directly |
| Commands | `LOG DUMP`, `LOG CLEAR`, `LOG STATUS` |

## Storage Architecture

### Partition Changes

Add a dedicated LittleFS partition to `partitions.csv`:

- Size: 1MB (1,048,576 bytes)
- Location: After OTA partition
- Name: `logdata`

### Log File Structure

Single file `/log.bin` using a ring buffer approach:

- Fixed-size header (32 bytes): magic number, write position, entry count, wrap flag
- Data region: sequential binary entries

### Binary Entry Format (16 bytes)

| Field | Type | Bytes |
|-------|------|-------|
| Timestamp (ms since boot) | uint32_t | 4 |
| Raw IR value | uint32_t | 4 |
| Agtron result | int16_t | 2 |
| LED brightness | uint8_t | 1 |
| Intersection point | uint8_t | 1 |
| Deviation x 1000 | uint16_t | 2 |
| Reserved/flags | uint16_t | 2 |

### Capacity

~65,000 entries per MB. At 10 readings/second active use, ~1.8 hours continuous. With typical idle time between roasts, weeks of data.

## Firmware Implementation

### Compile-Time Toggles

```cpp
#define DEBUG_LOGGING_ENABLED 1    // Log measurements to flash
#define DEBUG_SERIAL_COMMANDS 1    // Enable LOG DUMP/CLEAR/STATUS commands
```

| LOGGING | SERIAL_COMMANDS | Scenario |
|---------|-----------------|----------|
| 1 | 1 | Beta units - full debug |
| 1 | 0 | Silent logging - retrieve via future feature |
| 0 | 1 | No logging, commands return "logging disabled" |
| 0 | 0 | Production - no debug overhead |

### New Functions

```cpp
void setupDebugLog()        // Mount LittleFS, open/create log file, read header
void logMeasurement(...)    // Write entry to ring buffer, update header
void handleSerialCommands() // Check for LOG DUMP/CLEAR/STATUS commands
void dumpLogToSerial()      // Stream all entries as CSV
void clearLog()             // Reset header, mark log empty
```

### Integration Points

1. `setup()` - Call `setupDebugLog()` after preferences load
2. `measureSampleJob()` - Call `logMeasurement()` after valid Agtron calculation
3. `loop()` - Add `handleSerialCommands()` check at start

### Write Strategy

Buffer 10 entries in RAM, flush to flash every ~1 second or on idle. Reduces flash wear and doesn't block measurements.

## Data Retrieval

### Serial Commands

| Command | Action |
|---------|--------|
| `LOG DUMP` | Decode and output all entries as CSV |
| `LOG CLEAR` | Erase log, reset to empty |
| `LOG STATUS` | Show entry count, capacity %, wrap count |

### Dump Output Format

```
=== ROAST METER LOG DUMP ===
ENTRIES: 4523
WRAPPED: NO
--- BEGIN CSV ---
timestamp_ms,raw_ir,agtron,led_brightness,intersection_point,deviation
12345678,58432,87,95,117,0.165
12345789,58401,87,95,117,0.165
...
--- END CSV ---
```

No decoder script needed - copy/paste into `.csv` file.

### Optional Helper Script

`tools/capture_log.py` - Opens serial, sends `LOG DUMP`, saves CSV directly. Convenience only, not required.

### User Workflow

1. Connect USB, open serial terminal
2. Type `LOG DUMP`, press enter
3. Copy output to text file
4. Open CSV portion in Excel/Python

## Robustness

### Flash Wear Protection

- Buffer 10 entries in RAM before writing (10x fewer write cycles)
- Flush on idle (no measurement for 2 seconds)
- LittleFS handles wear leveling internally

### Power Loss Recovery

- Header updated after each flush with current write position
- On boot, read header to resume at correct position
- Worst case: lose last 10 unbuffered entries

### Corruption Detection

- Magic number in header (`0x524F5354` = "ROST") validates log file
- If corrupted/missing, create fresh log file
- Log version field for future format changes

### Memory Constraints

- RAM buffer: 10 entries x 16 bytes = 160 bytes
- Minimal impact on existing firmware

### Dump Performance

- Streams entries, doesn't load entire log to RAM
- At 115200 baud: ~10KB/sec
- Full 1MB log dumps in ~2 minutes

## Future Considerations

Not included in initial implementation:

- Temperature/battery voltage logging (when hardware supports it)
- Deduplication of consecutive identical readings
- WiFi upload option
- SD card support for XIAO expansion board

## Implementation Checklist

- [ ] Create custom partition table with 1MB logdata partition
- [ ] Add LittleFS library dependency to platformio.ini
- [ ] Implement log header struct and read/write functions
- [ ] Implement binary entry struct and ring buffer logic
- [ ] Add RAM buffer with flush-on-idle logic
- [ ] Implement `setupDebugLog()` with corruption recovery
- [ ] Implement `logMeasurement()` call in measureSampleJob()
- [ ] Implement serial command parser in loop()
- [ ] Implement `LOG DUMP` with CSV output
- [ ] Implement `LOG CLEAR` and `LOG STATUS`
- [ ] Add compile-time toggles with #ifdef guards
- [ ] Test on LOLIN S2 Mini
- [ ] Test on XIAO ESP32-C3
- [ ] Create optional capture_log.py helper script
