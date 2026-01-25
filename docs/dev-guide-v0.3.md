# Roast Meter v0.3 - Developer Update Guide

**Document Version:** 1.0  
**Current Firmware:** v0.2  
**Target Firmware:** v0.3  
**Date:** January 2026

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Configuration](#2-hardware-configuration)
3. [Code Changes Required](#3-code-changes-required)
4. [Calibration System](#4-calibration-system)
5. [Serial Command Interface](#5-serial-command-interface)
6. [Testing Protocol](#6-testing-protocol)
7. [Sample Preparation Standards](#7-sample-preparation-standards)

---

## 1. Overview

### Current State (v0.2)
- Uses **IR channel only** (880nm)
- Simple linear mapping formula
- Single intersection point calibration
- No serial command interface

### Target State (v0.3)
- Uses **Red + IR channels** (660nm + 880nm)
- **Ratio-based measurement** (Red/IR) for stability
- **Multi-point calibration** with interpolation
- **Serial command interface** for calibration and diagnostics
- Improved repeatability and accuracy

### Why Ratio-Based Measurement?

| Factor | IR Only (v0.2) | Red/IR Ratio (v0.3) |
|--------|----------------|---------------------|
| LED brightness drift | ❌ Affects reading | ✅ Cancels out |
| Sample distance variation | ❌ Affects reading | ✅ Mostly cancels |
| Device-to-device variation | ❌ Requires per-device cal | ✅ More consistent |
| Temperature drift | ❌ Affects reading | ✅ Reduced impact |

---

## 2. Hardware Configuration

### Sensor: MAX30102/MAX30105

**Current Configuration (v0.2):**
```cpp
particleSensor.setPulseAmplitudeRed(0);      // OFF
particleSensor.setPulseAmplitudeGreen(0);    // OFF
particleSensor.enableSlot(2, 0x02);          // IR only
```

**New Configuration (v0.3):**
```cpp
particleSensor.setPulseAmplitudeRed(ledBrightness);   // ON
particleSensor.setPulseAmplitudeIR(ledBrightness);    // ON
particleSensor.setPulseAmplitudeGreen(0);             // OFF
```

### Sample Chamber Specifications

```
┌─────────────────────┐
│      Tamper         │  ← User applies ~1-2kg pressure
├─────────────────────┤
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
│▓▓  Ground Coffee  ▓▓│  ← 10mm depth, pressed flat
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
├─────────────────────┤
│   Polycarbonate     │  ← 1mm clear sheet
├─────────────────────┤
│      MAX30102       │  ← Sensor facing up
└─────────────────────┘

Cup diameter: 20mm
Cup depth: 10mm
Material: Black/opaque (light blocking)
```

---

## 3. Code Changes Required

### 3.1 New Constants and Preferences

Add to header section:

```cpp
// -- Firmware Version --
#ifndef FIRMWARE_REVISION_STRING
#define FIRMWARE_REVISION_STRING "v0.3"
#endif

// -- Calibration Constants --
#define CAL_POINTS 5
#define PREF_CAL_RATIO_KEY "cal_ratio"      // Calibration ratio points
#define PREF_CAL_AGTRON_KEY "cal_agtron"    // Calibration Agtron points
#define PREF_USE_RATIO_KEY "use_ratio"      // Use ratio mode (bool)

// -- Measurement Settings --
#define SAMPLE_STABILIZE_MS 500    // Wait time for stable reading
#define READING_SAMPLES 10         // Number of readings to average
#define READING_TOLERANCE 0.02     // Max variation for stable reading (2%)

// -- Default Calibration Points --
// Format: {ratio, agtron} - determine empirically with reference device
const float DEFAULT_CAL_RATIO[CAL_POINTS] =  {0.45, 0.55, 0.65, 0.75, 0.85};
const int DEFAULT_CAL_AGTRON[CAL_POINTS] =   {35,   50,   65,   80,   95};
```

### 3.2 New Global Variables

```cpp
// -- Calibration Data --
float calRatio[CAL_POINTS];
int calAgtron[CAL_POINTS];
bool useRatioMode = true;

// -- Measurement State --
enum MeasureState {
    STATE_IDLE,
    STATE_STABILIZING,
    STATE_MEASURING,
    STATE_DISPLAY
};
MeasureState currentState = STATE_IDLE;
unsigned long stateTimer = 0;

// -- Running Average --
float ratioBuffer[READING_SAMPLES];
int bufferIndex = 0;
bool bufferFull = false;
```

### 3.3 Updated setupParticleSensor()

```cpp
void setupParticleSensor() {
    // Configure sensor with both Red and IR enabled
    particleSensor.setup(
        ledBrightness,  // LED power
        sampleAverage,  // Averaging
        2,              // LED mode: 2 = Red + IR
        sampleRate,     
        pulseWidth,     
        adcRange
    );
    
    // Enable both LEDs at same brightness for ratio measurement
    particleSensor.setPulseAmplitudeRed(ledBrightness);
    particleSensor.setPulseAmplitudeIR(ledBrightness);
    particleSensor.setPulseAmplitudeGreen(0);
    
    Serial.println("Sensor configured: Red + IR mode");
    Serial.println("LED Brightness: " + String(ledBrightness));
}
```

### 3.4 New Calibration Functions

```cpp
void loadCalibration() {
    // Load calibration points from preferences
    // If not valid, use defaults
    
    bool hasCalibration = preferences.getBool("cal_valid", false);
    
    if (hasCalibration) {
        for (int i = 0; i < CAL_POINTS; i++) {
            String ratioKey = "cal_r" + String(i);
            String agtronKey = "cal_a" + String(i);
            calRatio[i] = preferences.getFloat(ratioKey.c_str(), DEFAULT_CAL_RATIO[i]);
            calAgtron[i] = preferences.getInt(agtronKey.c_str(), DEFAULT_CAL_AGTRON[i]);
        }
        Serial.println("Calibration loaded from storage");
    } else {
        // Use defaults
        for (int i = 0; i < CAL_POINTS; i++) {
            calRatio[i] = DEFAULT_CAL_RATIO[i];
            calAgtron[i] = DEFAULT_CAL_AGTRON[i];
        }
        Serial.println("Using default calibration");
    }
    
    // Print calibration table
    Serial.println("Calibration Table:");
    Serial.println("Ratio\t\tAgtron");
    for (int i = 0; i < CAL_POINTS; i++) {
        Serial.print(calRatio[i], 3);
        Serial.print("\t\t");
        Serial.println(calAgtron[i]);
    }
}

void saveCalibration() {
    for (int i = 0; i < CAL_POINTS; i++) {
        String ratioKey = "cal_r" + String(i);
        String agtronKey = "cal_a" + String(i);
        preferences.putFloat(ratioKey.c_str(), calRatio[i]);
        preferences.putInt(agtronKey.c_str(), calAgtron[i]);
    }
    preferences.putBool("cal_valid", true);
    Serial.println("Calibration saved");
}

void resetCalibration() {
    for (int i = 0; i < CAL_POINTS; i++) {
        calRatio[i] = DEFAULT_CAL_RATIO[i];
        calAgtron[i] = DEFAULT_CAL_AGTRON[i];
    }
    preferences.putBool("cal_valid", false);
    Serial.println("Calibration reset to defaults");
}
```

### 3.5 New Mapping Function (Ratio-Based with Interpolation)

```cpp
int mapRatioToAgtron(float ratio) {
    // Handle out-of-range values
    if (ratio <= calRatio[0]) {
        // Extrapolate below lowest point
        float slope = (float)(calAgtron[1] - calAgtron[0]) / (calRatio[1] - calRatio[0]);
        int result = calAgtron[0] + (int)(slope * (ratio - calRatio[0]));
        return constrain(result, 15, 130);
    }
    
    if (ratio >= calRatio[CAL_POINTS - 1]) {
        // Extrapolate above highest point
        float slope = (float)(calAgtron[CAL_POINTS-1] - calAgtron[CAL_POINTS-2]) / 
                      (calRatio[CAL_POINTS-1] - calRatio[CAL_POINTS-2]);
        int result = calAgtron[CAL_POINTS-1] + (int)(slope * (ratio - calRatio[CAL_POINTS-1]));
        return constrain(result, 15, 130);
    }
    
    // Find segment and interpolate
    for (int i = 0; i < CAL_POINTS - 1; i++) {
        if (ratio >= calRatio[i] && ratio <= calRatio[i + 1]) {
            float t = (ratio - calRatio[i]) / (calRatio[i + 1] - calRatio[i]);
            float result = calAgtron[i] + t * (calAgtron[i + 1] - calAgtron[i]);
            return constrain((int)round(result), 15, 130);
        }
    }
    
    // Fallback (should not reach here)
    return 65;
}

// Keep legacy function for backward compatibility
int mapIRToAgtron(uint32_t x) {
    int scaledX = (int)x;
    float result = scaledX - (intersectionPoint - scaledX) * deviation;
    return constrain((int)round(result), 15, 130);
}
```

### 3.6 Updated Measurement Function

```cpp
// Measurement structure for cleaner data handling
struct Measurement {
    uint32_t red;
    uint32_t ir;
    float ratio;
    int agtron;
    bool valid;
};

Measurement takeMeasurement() {
    Measurement m;
    m.valid = false;
    
    // Take multiple readings and average
    uint32_t redSum = 0;
    uint32_t irSum = 0;
    int validReadings = 0;
    
    for (int i = 0; i < READING_SAMPLES; i++) {
        uint32_t red = particleSensor.getRed();
        uint32_t ir = particleSensor.getIR();
        
        // Validate individual reading
        if (red > 1000 && red < 500000 && ir > 1000 && ir < 500000) {
            redSum += red;
            irSum += ir;
            validReadings++;
        }
        delay(10);  // Small delay between readings
    }
    
    if (validReadings < READING_SAMPLES / 2) {
        Serial.println("Warning: Too many invalid readings");
        return m;
    }
    
    m.red = redSum / validReadings;
    m.ir = irSum / validReadings;
    
    // Calculate ratio
    if (m.ir > 0) {
        m.ratio = (float)m.red / (float)m.ir;
    } else {
        return m;
    }
    
    // Convert to Agtron
    if (useRatioMode) {
        m.agtron = mapRatioToAgtron(m.ratio);
    } else {
        // Legacy IR-only mode
        m.agtron = mapIRToAgtron(m.ir / 1000);
    }
    
    m.valid = true;
    return m;
}

unsigned long measureSampleJobTimer = millis();
void measureSampleJob() {
    if (millis() - measureSampleJobTimer < 100) {
        return;
    }
    measureSampleJobTimer = millis();
    
    // Quick check if sample is present
    uint32_t ir = particleSensor.getIR();
    long delta = (long)ir - (long)unblockedValue;
    
    if (delta <= 100) {
        // No sample detected
        displayPleaseLoadSample();
        currentState = STATE_IDLE;
        bufferIndex = 0;
        bufferFull = false;
        return;
    }
    
    // Sample detected - take measurement
    Measurement m = takeMeasurement();
    
    if (!m.valid) {
        displayPleaseLoadSample();
        return;
    }
    
    // Display result
    displayMeasurement(m.agtron);
    
    // Debug output
    Serial.println("--- Measurement ---");
    Serial.println("Red: " + String(m.red));
    Serial.println("IR: " + String(m.ir));
    Serial.println("Ratio: " + String(m.ratio, 4));
    Serial.println("Agtron: " + String(m.agtron));
    Serial.println("-------------------");
}
```

---

## 4. Calibration System

### 4.1 Calibration Workflow

```
┌─────────────────────────────────────────────────────────────┐
│                    CALIBRATION PROCESS                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Prepare 5 coffee samples at different roast levels       │
│     (Dark → Light, covering Agtron 35-95 range)             │
│                                                              │
│  2. Measure each sample on REFERENCE Agtron device           │
│     Record: Sample A = 38, Sample B = 52, etc.              │
│                                                              │
│  3. Connect device to Serial (115200 baud)                   │
│                                                              │
│  4. For each sample:                                         │
│     a. Load sample into cup (pressed)                        │
│     b. Wait for stable reading                               │
│     c. Send command: CAL <agtron_value>                      │
│        Example: CAL 38                                       │
│     d. Device records ratio → agtron mapping                 │
│                                                              │
│  5. After all points recorded, send: SAVE                    │
│                                                              │
│  6. Verify with: DUMP                                        │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Calibration Data Structure

```cpp
// Temporary calibration storage during calibration process
struct CalibrationPoint {
    float ratio;
    int agtron;
    bool set;
};

CalibrationPoint tempCalPoints[CAL_POINTS];
int tempCalIndex = 0;

void addCalibrationPoint(int knownAgtron) {
    Measurement m = takeMeasurement();
    
    if (!m.valid) {
        Serial.println("ERROR: Cannot take measurement. Check sample.");
        return;
    }
    
    if (tempCalIndex >= CAL_POINTS) {
        Serial.println("ERROR: Maximum calibration points reached.");
        Serial.println("Use SAVE to store or CLEAR to restart.");
        return;
    }
    
    tempCalPoints[tempCalIndex].ratio = m.ratio;
    tempCalPoints[tempCalIndex].agtron = knownAgtron;
    tempCalPoints[tempCalIndex].set = true;
    
    Serial.println("Calibration point " + String(tempCalIndex + 1) + " recorded:");
    Serial.println("  Ratio: " + String(m.ratio, 4));
    Serial.println("  Agtron: " + String(knownAgtron));
    
    tempCalIndex++;
    
    if (tempCalIndex >= CAL_POINTS) {
        Serial.println("All calibration points recorded. Send SAVE to store.");
    } else {
        Serial.println("Points recorded: " + String(tempCalIndex) + "/" + String(CAL_POINTS));
    }
}

void finalizeCalibration() {
    if (tempCalIndex < 3) {
        Serial.println("ERROR: Need at least 3 calibration points.");
        return;
    }
    
    // Sort by ratio (ascending)
    for (int i = 0; i < tempCalIndex - 1; i++) {
        for (int j = i + 1; j < tempCalIndex; j++) {
            if (tempCalPoints[i].ratio > tempCalPoints[j].ratio) {
                CalibrationPoint temp = tempCalPoints[i];
                tempCalPoints[i] = tempCalPoints[j];
                tempCalPoints[j] = temp;
            }
        }
    }
    
    // Copy to active calibration
    for (int i = 0; i < tempCalIndex && i < CAL_POINTS; i++) {
        calRatio[i] = tempCalPoints[i].ratio;
        calAgtron[i] = tempCalPoints[i].agtron;
    }
    
    // Fill remaining points with extrapolation if needed
    // ... (implementation depends on requirements)
    
    saveCalibration();
    
    Serial.println("Calibration finalized and saved!");
}
```

---

## 5. Serial Command Interface

### 5.1 Command List

| Command | Description | Example |
|---------|-------------|---------|
| `HELP` | Show available commands | `HELP` |
| `DUMP` | Show current reading (raw values) | `DUMP` |
| `STATUS` | Show device status and settings | `STATUS` |
| `CAL <agtron>` | Add calibration point | `CAL 65` |
| `SAVE` | Save calibration to flash | `SAVE` |
| `CLEAR` | Clear temporary calibration | `CLEAR` |
| `RESET` | Reset to default calibration | `RESET` |
| `TABLE` | Show calibration table | `TABLE` |
| `MODE <ratio/ir>` | Switch measurement mode | `MODE ratio` |
| `LED <0-255>` | Set LED brightness | `LED 100` |
| `TEST` | Run self-test | `TEST` |

### 5.2 Command Handler Implementation

```cpp
void checkSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd == "HELP") {
        printHelp();
    }
    else if (cmd == "DUMP") {
        dumpCurrentReading();
    }
    else if (cmd == "STATUS") {
        printStatus();
    }
    else if (cmd.startsWith("CAL ")) {
        int agtron = cmd.substring(4).toInt();
        if (agtron >= 15 && agtron <= 130) {
            addCalibrationPoint(agtron);
        } else {
            Serial.println("ERROR: Agtron must be 15-130");
        }
    }
    else if (cmd == "SAVE") {
        finalizeCalibration();
    }
    else if (cmd == "CLEAR") {
        clearTempCalibration();
    }
    else if (cmd == "RESET") {
        resetCalibration();
    }
    else if (cmd == "TABLE") {
        printCalibrationTable();
    }
    else if (cmd.startsWith("MODE ")) {
        String mode = cmd.substring(5);
        if (mode == "RATIO") {
            useRatioMode = true;
            Serial.println("Mode: RATIO (Red/IR)");
        } else if (mode == "IR") {
            useRatioMode = false;
            Serial.println("Mode: IR only (legacy)");
        }
    }
    else if (cmd.startsWith("LED ")) {
        int brightness = cmd.substring(4).toInt();
        if (brightness >= 0 && brightness <= 255) {
            ledBrightness = brightness;
            setupParticleSensor();
            Serial.println("LED brightness set to: " + String(brightness));
        }
    }
    else if (cmd == "TEST") {
        runSelfTest();
    }
    else {
        Serial.println("Unknown command. Type HELP for list.");
    }
}

void printHelp() {
    Serial.println("=== Roast Meter Commands ===");
    Serial.println("HELP       - Show this help");
    Serial.println("DUMP       - Show current raw reading");
    Serial.println("STATUS     - Show device status");
    Serial.println("CAL <val>  - Add calibration point (e.g., CAL 65)");
    Serial.println("SAVE       - Save calibration");
    Serial.println("CLEAR      - Clear temp calibration");
    Serial.println("RESET      - Reset to defaults");
    Serial.println("TABLE      - Show calibration table");
    Serial.println("MODE ratio - Use Red/IR ratio mode");
    Serial.println("MODE ir    - Use IR-only mode (legacy)");
    Serial.println("LED <0-255>- Set LED brightness");
    Serial.println("TEST       - Run self-test");
    Serial.println("============================");
}

void dumpCurrentReading() {
    Measurement m = takeMeasurement();
    
    Serial.println("=== Current Reading ===");
    Serial.println("Red Raw:    " + String(m.red));
    Serial.println("IR Raw:     " + String(m.ir));
    Serial.println("Ratio:      " + String(m.ratio, 4));
    Serial.println("Agtron:     " + String(m.agtron));
    Serial.println("Valid:      " + String(m.valid ? "Yes" : "No"));
    Serial.println("=======================");
}

void printStatus() {
    Serial.println("=== Device Status ===");
    Serial.println("Firmware:   " + String(FIRMWARE_REVISION_STRING));
    Serial.println("Mode:       " + String(useRatioMode ? "RATIO" : "IR"));
    Serial.println("LED Power:  " + String(ledBrightness));
    Serial.println("Cal Points: " + String(CAL_POINTS));
    Serial.println("Warmup:     " + String(WARMUP_TIME) + "s");
    Serial.println("=====================");
}

void printCalibrationTable() {
    Serial.println("=== Calibration Table ===");
    Serial.println("Point\tRatio\t\tAgtron");
    for (int i = 0; i < CAL_POINTS; i++) {
        Serial.print(i + 1);
        Serial.print("\t");
        Serial.print(calRatio[i], 4);
        Serial.print("\t\t");
        Serial.println(calAgtron[i]);
    }
    Serial.println("=========================");
}

void runSelfTest() {
    Serial.println("=== Self Test ===");
    
    // Test 1: Sensor communication
    Serial.print("Sensor: ");
    uint32_t ir = particleSensor.getIR();
    if (ir > 0 && ir < 1000000) {
        Serial.println("OK (" + String(ir) + ")");
    } else {
        Serial.println("FAIL");
    }
    
    // Test 2: OLED
    Serial.print("OLED: ");
    Serial.println(oledAvailable ? "OK" : "NOT FOUND");
    
    // Test 3: Preferences
    Serial.print("Storage: ");
    byte testVal = preferences.getUChar(PREF_VALID_KEY, 0);
    Serial.println(testVal == PREF_VALID_CODE ? "OK" : "NOT INITIALIZED");
    
    // Test 4: Both LEDs
    Serial.print("Red LED: ");
    particleSensor.setPulseAmplitudeRed(ledBrightness);
    delay(100);
    uint32_t red = particleSensor.getRed();
    Serial.println(red > 0 ? "OK" : "FAIL");
    
    Serial.print("IR LED: ");
    Serial.println(ir > 0 ? "OK" : "FAIL");
    
    Serial.println("=================");
}
```

### 5.3 Add to Main Loop

```cpp
void loop() {
    checkSerialCommands();  // Add this line
    measureSampleJob();
}
```

---

## 6. Testing Protocol

### 6.1 Unit Tests (Developer)

```cpp
void runUnitTests() {
    Serial.println("=== Running Unit Tests ===");
    
    // Test 1: Ratio mapping
    Serial.println("Test 1: Ratio Mapping");
    float testRatios[] = {0.40, 0.50, 0.60, 0.70, 0.80, 0.90};
    for (float r : testRatios) {
        int a = mapRatioToAgtron(r);
        Serial.println("  Ratio " + String(r, 2) + " -> Agtron " + String(a));
    }
    
    // Test 2: Edge cases
    Serial.println("Test 2: Edge Cases");
    Serial.println("  Ratio 0.20 -> " + String(mapRatioToAgtron(0.20)));  // Below range
    Serial.println("  Ratio 1.00 -> " + String(mapRatioToAgtron(1.00)));  // Above range
    
    // Test 3: Interpolation accuracy
    Serial.println("Test 3: Interpolation");
    // Should return exact values at calibration points
    for (int i = 0; i < CAL_POINTS; i++) {
        int result = mapRatioToAgtron(calRatio[i]);
        bool pass = (result == calAgtron[i]);
        Serial.println("  Point " + String(i) + ": " + String(pass ? "PASS" : "FAIL"));
    }
    
    Serial.println("=========================");
}
```

### 6.2 Acceptance Tests (QA)

| Test | Procedure | Expected Result |
|------|-----------|-----------------|
| Power On | Power device, wait 60s | Display shows "Please load sample" |
| Sample Detection | Load sample into cup | Display shows Agtron value |
| Sample Removal | Remove sample | Display returns to "Please load sample" |
| Repeatability | Measure same sample 5 times | Values within ±2 Agtron |
| Range Test | Test samples from 35-95 Agtron | All readable, monotonic increase |
| Serial DUMP | Send DUMP command | Shows Red, IR, Ratio, Agtron |
| Calibration | Complete calibration procedure | Values match reference within ±3 |

### 6.3 Repeatability Test Script

```cpp
void repeatabilityTest() {
    Serial.println("=== Repeatability Test ===");
    Serial.println("Keep sample loaded. Taking 10 measurements...");
    
    int readings[10];
    float ratios[10];
    
    for (int i = 0; i < 10; i++) {
        delay(500);
        Measurement m = takeMeasurement();
        readings[i] = m.agtron;
        ratios[i] = m.ratio;
        Serial.println("Reading " + String(i+1) + ": " + String(m.agtron) + 
                       " (ratio: " + String(m.ratio, 4) + ")");
    }
    
    // Calculate statistics
    float sum = 0, ratioSum = 0;
    for (int i = 0; i < 10; i++) {
        sum += readings[i];
        ratioSum += ratios[i];
    }
    float mean = sum / 10;
    float ratioMean = ratioSum / 10;
    
    float variance = 0;
    for (int i = 0; i < 10; i++) {
        variance += pow(readings[i] - mean, 2);
    }
    float stdDev = sqrt(variance / 10);
    
    Serial.println("--- Results ---");
    Serial.println("Mean Agtron: " + String(mean, 1));
    Serial.println("Std Dev: " + String(stdDev, 2));
    Serial.println("Range: " + String(*min_element(readings, readings+10)) + 
                   " - " + String(*max_element(readings, readings+10)));
    Serial.println("PASS: " + String(stdDev < 2.0 ? "YES" : "NO"));
    Serial.println("=========================");
}
```

---

## 7. Sample Preparation Standards

### 7.1 Coffee Preparation

| Parameter | Specification |
|-----------|---------------|
| Grind Size | 400-500 microns (cupping grind) |
| Sample Amount | Fill cup completely |
| Pressing | Light press with tamper (~1-2 kg) |
| Surface | Flat, level, no gaps |
| Temperature | Room temperature (let cool if fresh roasted) |

### 7.2 Measurement Procedure (User Instructions)

```
1. Grind coffee to cupping grind size (medium-coarse)
2. Fill sample cup completely (heaping OK)
3. Level off excess with straight edge
4. Press lightly with tamper until flat
5. Place cup on sensor
6. Wait 2-3 seconds for stable reading
7. Record Agtron value
8. Clean polycarbonate window periodically
```

### 7.3 Tamper Design

```
    ┌─────────┐
    │  Handle │  Height: 20mm
    │    ○    │
    ├─────────┤
    │ ▓▓▓▓▓▓▓ │  Tamper head
    └─────────┘  Diameter: 18mm (for 20mm cup)
                 Material: PLA/ABS (3D printed)
```

---

## Appendix A: Full Updated main.cpp

See separate file: `main_v0.3.cpp`

---

## Appendix B: Migration Checklist

- [ ] Update FIRMWARE_REVISION_STRING to "v0.3"
- [ ] Add new constants and preferences keys
- [ ] Update setupParticleSensor() for dual LED
- [ ] Add calibration data structures and functions
- [ ] Implement mapRatioToAgtron()
- [ ] Update measureSampleJob() for ratio mode
- [ ] Add serial command handler
- [ ] Add checkSerialCommands() to main loop
- [ ] Test with reference Agtron device
- [ ] Record default calibration values
- [ ] Update DEFAULT_CAL_RATIO and DEFAULT_CAL_AGTRON arrays

---

## Appendix C: Troubleshooting

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| No reading | Sample not detected | Check delta threshold, adjust unblockedValue |
| Unstable readings | Ambient light leak | Ensure cup is opaque, check seals |
| Readings too high/low | Calibration off | Re-calibrate with reference device |
| Red LED not working | Configuration error | Check setPulseAmplitudeRed() call |
| Serial not responding | Baud rate mismatch | Ensure 115200 baud |

---

**Document End**
