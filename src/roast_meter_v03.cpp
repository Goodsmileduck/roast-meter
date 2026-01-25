// VERSION 0.3 - Ratio-based measurement with serial commands
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "MAX30105.h"

// -- Board Configuration (override via build_flags) --
#ifndef I2C_SDA
#define I2C_SDA -1
#endif
#ifndef I2C_SCL
#define I2C_SCL -1
#endif

// -- Display Configuration (override via build_flags) --
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif
#ifndef I2C_ADDRESS_OLED
#define I2C_ADDRESS_OLED 0x3C
#endif

// -- Constant Values --
#ifndef FIRMWARE_REVISION_STRING
#define FIRMWARE_REVISION_STRING "v0.3"
#endif

#define WARMUP_TIME 60  // seconds

// -- Calibration Constants --
#define CAL_POINTS 5
#define READING_SAMPLES 10

// -- Default Calibration Points --
// Format: {ratio, agtron} - determine empirically with reference device
const float DEFAULT_CAL_RATIO[CAL_POINTS] =  {0.45, 0.55, 0.65, 0.75, 0.85};
const int DEFAULT_CAL_AGTRON[CAL_POINTS] =   {35,   50,   65,   80,   95};

// -- End Constant Values --

// -- Preferences constants --

#define PREF_NAMESPACE "roast_meter"
#define PREF_VALID_KEY "valid"
#define PREF_VALID_CODE (0xAB)  // Different from v0.2 to force re-init
#define PREF_LED_BRIGHTNESS_KEY "led_brightness"
#define PREF_LED_BRIGHTNESS_DEFAULT 95

#define OLED_RESET -1

// -- End Preferences constants

// -- Global Variables --

uint32_t unblockedValue = 30000;  // Average IR at power up

MAX30105 particleSensor;

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Preferences preferences;

// OLED status tracking
bool oledAvailable = false;

// -- Calibration Data --
float calRatio[CAL_POINTS];
int calAgtron[CAL_POINTS];
bool useRatioMode = true;

// Temporary calibration storage during calibration process
struct CalibrationPoint {
    float ratio;
    int agtron;
    bool set;
};
CalibrationPoint tempCalPoints[CAL_POINTS];
int tempCalIndex = 0;

// -- Measurement structure --
struct Measurement {
    uint32_t red;
    uint32_t ir;
    float ratio;
    int agtron;
    bool valid;
};

// -- End Global Variables --

// -- Global Setting --

byte ledBrightness = 95;      // !Preferences setup
byte sampleAverage = 4;  // Options: 1, 2, 4, 8, 16, --32--
byte ledMode = 2;        // Options: 1 = Red only, --2 = Red + IR--, 3 = Red + IR + Green
int sampleRate = 50;     // Options: 50, 100, 200, 400, 800, 1000, 1600, --3200--
int pulseWidth = 411;    // Options: 69, 118, 215, --411--
int adcRange = 16384;    // Options: 2048, 4096, 8192, --16384--

// Legacy v0.2 calibration (for IR-only mode fallback)
int intersectionPoint = 117;
float deviation = 0.165;

// -- End Global Setting --

// -- Setup Headers --

void setupPreferences();
void setupParticleSensor();
void loadCalibration();
void saveCalibration();
void resetCalibration();

// -- Sub Routine Headers --

void displayStartUp();
void warmUpLED();
void measureSampleJob();
void displayPleaseLoadSample();
void displayMeasurement(int rLevel);
Measurement takeMeasurement();

// -- Serial Command Headers --
void checkSerialCommands();
void printHelp();
void dumpCurrentReading();
void printStatus();
void printCalibrationTable();
void addCalibrationPoint(int knownAgtron);
void finalizeCalibration();
void clearTempCalibration();
void runSelfTest();

// -- Utility Function Headers --

int mapRatioToAgtron(float ratio);
int mapIRToAgtron(uint32_t x);

// -- Main Process --
void setup() {
    Serial.begin(115200);

#if I2C_SDA >= 0 && I2C_SCL >= 0
    Wire.begin(I2C_SDA, I2C_SCL);
#else
    Wire.begin();
#endif

    // Initialize OLED
    if (!oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS_OLED)) {
        Serial.println(F("OLED initialization failed!"));
        Serial.println(F("Continuing without display..."));
        oledAvailable = false;
    } else {
        oledAvailable = true;
        Serial.println(F("OLED initialized successfully"));

        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(WHITE);
        oled.setCursor(0, 0);
        oled.println("Initializing...");
        oled.display();
    }

    setupPreferences();
    loadCalibration();

    // Initialize sensor
    if (particleSensor.begin(Wire, 400000) == false) {
        Serial.println("MAX30105 was not found. Please check wiring/power.");
        if (oledAvailable) {
            oled.clearDisplay();
            oled.setCursor(0, 0);
            oled.println("Sensor Error!");
            oled.println("Check wiring");
            oled.display();
        }

        // Retry every 5 seconds
        while (particleSensor.begin(Wire, 400000) == false) {
            Serial.println("Retrying sensor initialization...");
            delay(5000);
        }
        Serial.println("Sensor initialized after retry!");
    }

    setupParticleSensor();

    displayStartUp();
    warmUpLED();

    Serial.println("Type HELP for available commands");
}

void loop() {
    checkSerialCommands();
    measureSampleJob();
}

// -- End Main Process --

// -- Setups --

void setupPreferences() {
    preferences.begin(PREF_NAMESPACE, false);

    if (preferences.getUChar(PREF_VALID_KEY, 0) != PREF_VALID_CODE) {
        Serial.println("Preferences were invalid (v0.3 format)");

        preferences.putUChar(PREF_VALID_KEY, PREF_VALID_CODE);
        preferences.putUChar(PREF_LED_BRIGHTNESS_KEY, PREF_LED_BRIGHTNESS_DEFAULT);
        preferences.putBool("cal_valid", false);

        Serial.println("Preferences initialized for v0.3");
    }

    Serial.println("Preferences are valid");

    ledBrightness = preferences.getUChar(PREF_LED_BRIGHTNESS_KEY, PREF_LED_BRIGHTNESS_DEFAULT);
    Serial.println("Set ledBrightness to " + String(ledBrightness));
}

void setupParticleSensor() {
    // Configure sensor with both Red and IR enabled
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

    // Enable both LEDs at same brightness for ratio measurement
    particleSensor.setPulseAmplitudeRed(ledBrightness);
    particleSensor.setPulseAmplitudeIR(ledBrightness);
    particleSensor.setPulseAmplitudeGreen(0);

    Serial.println("Sensor configured: Red + IR mode");
    Serial.println("LED Brightness: " + String(ledBrightness));
}

void loadCalibration() {
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

// -- End Setups --

// -- Mapping Functions --

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

int mapIRToAgtron(uint32_t x) {
    // Legacy IR-only mode (v0.2 compatible)
    int scaledX = (int)x;
    float result = scaledX - (intersectionPoint - scaledX) * deviation;
    return constrain((int)round(result), 15, 130);
}

// -- End Mapping Functions --

// -- Measurement Functions --

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

// -- End Measurement Functions --

// Sub Routines

void displayStartUp() {
    if (!oledAvailable) {
        Serial.println("Display: Roast Meter " + String(FIRMWARE_REVISION_STRING));
        delay(2000);
        return;
    }

    oled.setCursor(0, 0);
    oled.clearDisplay();
    oled.print("Roast  ");
    oled.print("Meter  ");
    oled.print(FIRMWARE_REVISION_STRING);
    oled.display();

    delay(2000);
}

const char* getWarmupFace(int secondsLeft) {
    if (secondsLeft > 45) return "(-.-)zzZ";  // sleeping
    if (secondsLeft > 30) return "(-.-)z";    // drowsy
    if (secondsLeft > 15) return "(o.o)";     // waking
    if (secondsLeft > 5)  return "(^.^)";     // alert
    return "(^o^)/";                           // ready!
}

void warmUpLED() {
    int countDownSeconds = WARMUP_TIME;
    unsigned long jobTimerStart = millis();
    unsigned long jobTimer = jobTimerStart;

    while (millis() - jobTimerStart <= WARMUP_TIME * 1000) {
        unsigned long elapsed = millis() - jobTimer;

        if (elapsed > 100) {
            countDownSeconds = WARMUP_TIME - ((millis() - jobTimerStart) / 1000);

            if (oledAvailable) {
                oled.clearDisplay();
                oled.setTextSize(1);

#if SCREEN_WIDTH <= 64
                // 64x48 display
                oled.setCursor(0, 0);
                oled.println(getWarmupFace(countDownSeconds));
                oled.println();
                oled.printf("Warm %ds", countDownSeconds);
#else
                // 128x64 display
                oled.setCursor(0, 8);
                oled.setTextSize(2);
                oled.println(getWarmupFace(countDownSeconds));
                oled.setTextSize(1);
                oled.println();
                oled.printf("  Warming up %ds", countDownSeconds);
#endif
                oled.display();
            } else {
                Serial.printf("Warm Up %ds %s\n", countDownSeconds, getWarmupFace(countDownSeconds));
            }

            jobTimer = millis();
        }
    }

    // Ready celebration screen
    if (oledAvailable) {
        oled.clearDisplay();
#if SCREEN_WIDTH <= 64
        oled.setTextSize(1);
        oled.setCursor(0, 12);
        oled.println(" (^o^)/");
        oled.println();
        oled.println(" Ready!");
#else
        oled.setTextSize(2);
        oled.setCursor(20, 10);
        oled.println("(^o^)/");
        oled.setCursor(28, 35);
        oled.println("Ready!");
#endif
        oled.display();
    } else {
        Serial.println("(^o^)/ Ready!");
    }
    delay(1500);
}

unsigned long measureSampleJobTimer = millis();
void measureSampleJob() {
    if (millis() - measureSampleJobTimer > 100) {
        // Quick check if sample is present using IR
        uint32_t ir = particleSensor.getIR();
        long delta = (long)ir - (long)unblockedValue;

        if (delta <= 100) {
            // No sample detected
            displayPleaseLoadSample();
            measureSampleJobTimer = millis();
            return;
        }

        // Sample detected - take full measurement
        Measurement m = takeMeasurement();

        if (!m.valid) {
            displayPleaseLoadSample();
            measureSampleJobTimer = millis();
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

        measureSampleJobTimer = millis();
    }
}

void displayPleaseLoadSample() {
    if (!oledAvailable) {
        return;  // Don't spam serial with this message
    }

    oled.clearDisplay();
    oled.setCursor(0, 0);

#if SCREEN_WIDTH <= 64
    // 64x48 (0.66" OLED)
    oled.setTextSize(1);
    oled.println("Please");
    oled.println("load");
    oled.println("sample!");
#else
    // 128x64 (0.96" OLED)
    oled.setTextSize(2);
    oled.println("Please ");
    oled.println("load ");
    oled.println("sample! ");
#endif

    oled.display();
}

void drawMyCenterString(const String &buf, int y){
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    oled.setCursor((oled.width() - w) / 2, y);
    oled.print(buf);
}

void displayMeasurement(int agtronLevel) {
    if (!oledAvailable) {
        return;  // Serial output handled in measureSampleJob
    }

    oled.clearDisplay();

    String agtronLevelText = String(agtronLevel);
#if SCREEN_WIDTH <= 64
    // 64x48 (0.66" OLED)
    oled.setTextSize(2);
    drawMyCenterString(agtronLevelText, 16);
#else
    // 128x64 (0.96" OLED)
    oled.setTextSize(3);
    drawMyCenterString(agtronLevelText, 20);
#endif

    oled.display();
}

// -- End Sub Routines --

// -- Serial Command Interface --

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
        } else {
            Serial.println("ERROR: Use MODE RATIO or MODE IR");
        }
    }
    else if (cmd.startsWith("LED ")) {
        int brightness = cmd.substring(4).toInt();
        if (brightness >= 0 && brightness <= 255) {
            ledBrightness = brightness;
            preferences.putUChar(PREF_LED_BRIGHTNESS_KEY, ledBrightness);
            setupParticleSensor();
            Serial.println("LED brightness set to: " + String(brightness));
        } else {
            Serial.println("ERROR: Brightness must be 0-255");
        }
    }
    else if (cmd == "TEST") {
        runSelfTest();
    }
    else if (cmd.length() > 0) {
        Serial.println("Unknown command. Type HELP for list.");
    }
}

void printHelp() {
    Serial.println("=== Roast Meter v0.3 Commands ===");
    Serial.println("HELP       - Show this help");
    Serial.println("DUMP       - Show current raw reading");
    Serial.println("STATUS     - Show device status");
    Serial.println("CAL <val>  - Add calibration point (e.g., CAL 65)");
    Serial.println("SAVE       - Save calibration");
    Serial.println("CLEAR      - Clear temp calibration");
    Serial.println("RESET      - Reset to defaults");
    Serial.println("TABLE      - Show calibration table");
    Serial.println("MODE RATIO - Use Red/IR ratio mode");
    Serial.println("MODE IR    - Use IR-only mode (legacy)");
    Serial.println("LED <0-255>- Set LED brightness");
    Serial.println("TEST       - Run self-test");
    Serial.println("=================================");
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
    Serial.println("OLED:       " + String(oledAvailable ? "Yes" : "No"));
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

    // Sort by ratio (ascending) using bubble sort
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

    // If fewer than CAL_POINTS, fill remaining with extrapolation
    if (tempCalIndex < CAL_POINTS) {
        float lastSlope = (float)(calAgtron[tempCalIndex-1] - calAgtron[tempCalIndex-2]) /
                          (calRatio[tempCalIndex-1] - calRatio[tempCalIndex-2]);
        for (int i = tempCalIndex; i < CAL_POINTS; i++) {
            calRatio[i] = calRatio[i-1] + 0.1;
            calAgtron[i] = calAgtron[i-1] + (int)(lastSlope * 0.1);
        }
    }

    saveCalibration();
    clearTempCalibration();

    Serial.println("Calibration finalized and saved!");
    printCalibrationTable();
}

void clearTempCalibration() {
    for (int i = 0; i < CAL_POINTS; i++) {
        tempCalPoints[i].set = false;
    }
    tempCalIndex = 0;
    Serial.println("Temporary calibration cleared");
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
    uint32_t red = particleSensor.getRed();
    Serial.println(red > 0 ? "OK (" + String(red) + ")" : "FAIL");

    Serial.print("IR LED: ");
    Serial.println(ir > 0 ? "OK" : "FAIL");

    // Test 5: Ratio calculation
    Serial.print("Ratio: ");
    if (red > 0 && ir > 0) {
        float ratio = (float)red / (float)ir;
        Serial.println(String(ratio, 4));
    } else {
        Serial.println("FAIL");
    }

    Serial.println("=================");
}

// -- End Serial Command Interface --
