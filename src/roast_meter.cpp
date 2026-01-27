// VERSION 0.2
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "MAX30105.h"
#include <LittleFS.h>

// -- Debug Logging Configuration --
#ifndef DEBUG_LOGGING_ENABLED
#define DEBUG_LOGGING_ENABLED 1
#endif
#ifndef DEBUG_SERIAL_COMMANDS
#define DEBUG_SERIAL_COMMANDS 1
#endif

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
// Y offset for displays where visible area is shifted (e.g., some 64x48 OLEDs)
#ifndef DISPLAY_Y_OFFSET
#define DISPLAY_Y_OFFSET 0
#endif

// -- Constant Values --
#ifndef FIRMWARE_REVISION_STRING
#define FIRMWARE_REVISION_STRING "v0.2"
#endif

#define WARMUP_TIME 60  // seconds

// -- End Constant Values --

// -- Preferences constants --

#define PREF_NAMESPACE "roast_meter"
#define PREF_VALID_KEY "valid"
#define PREF_VALID_CODE (0xAA)
#define PREF_LED_BRIGHTNESS_KEY "led_brightness"
#define PREF_LED_BRIGHTNESS_DEFAULT 95
#define PREF_INTERSECTION_POINT_KEY "intersection_point"
#define PREF_INTERSECTION_POINT_DEFAULT 117
#define PREF_DEVIATION_KEY "deviation"
#define PREF_DEVIATION_DEFAULT 0.165f

#define OLED_RESET -1

// -- End Preferences constants

// -- Debug Log constants --
#if DEBUG_LOGGING_ENABLED
#define LOG_FILE_PATH "/log.bin"
#define LOG_MAGIC 0x524F5354  // "ROST"
#define LOG_VERSION 1
#define LOG_MAX_ENTRIES 65000
#define LOG_BUFFER_SIZE 10
#define LOG_FLUSH_IDLE_MS 2000

// Log header structure (32 bytes)
struct __attribute__((packed)) LogHeader {
    uint32_t magic;           // 0x524F5354 "ROST"
    uint16_t version;         // Log format version
    uint16_t reserved1;       // Padding
    uint32_t writePosition;   // Next write index (0 to LOG_MAX_ENTRIES-1)
    uint32_t entryCount;      // Total entries written (can exceed MAX if wrapped)
    uint8_t  wrapped;         // 1 if buffer has wrapped
    uint8_t  reserved2[15];   // Padding to 32 bytes
};

// Log entry structure (16 bytes)
struct __attribute__((packed)) LogEntry {
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

// -- Global Variables --

uint32_t unblockedValue = 30000;  // Average IR at power up

MAX30105 particleSensor;


Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Preferences preferences;

// OLED status tracking
bool oledAvailable = false;

// -- End Global Variables --

// -- Global Setting --

byte ledBrightness = 95;      // !Preferences setup
byte sampleAverage = 4;  // Options: 1, 2, 4, 8, 16, --32--
byte ledMode = 2;        // Options: 1 = Red only, --2 = Red + IR--, 3 = Red + IR + Green
int sampleRate = 50;     // Options: 50, 100, 200, 400, 800, 1000, 1600, --3200--
int pulseWidth = 411;    // Options: 69, 118, 215, --411--
int adcRange = 16384;    // Options: 2048, 4096, 8192, --16384--

int intersectionPoint = 117;  // !Preferences setup
float deviation = 0.165;      // !Preferences setup

// -- End Global Setting --

// -- Setup Headers --

void setupPreferences();
void setupParticleSensor();

// -- Setup Headers --

// -- Sub Routine Headers --

void displayStartUp();
void warmUpLED();
void measureSampleJob();
void displayPleaseLoadSample();
void displayMeasurement(int rLevel);

// -- End Sub Routine Headers --

// -- Utility Function Headers --

int mapIRToAgtron(uint32_t x);

// -- End Utility Function Headers --

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
        Serial.println(F("❌ OLED initialization failed!"));
        // Continue without display - device can still work via serial
        Serial.println(F("Continuing without display..."));
        oledAvailable = false;
    } else {
        oledAvailable = true;
        Serial.println(F("✅ OLED initialized successfully"));

        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(WHITE);
        oled.setCursor(0, 0);
        oled.println("Initializing...");
        oled.display();
    }

    setupPreferences();

    // Initialize sensor
    if (particleSensor.begin(Wire, 400000) == false)  // Use default I2C port, 400kHz speed
    {
        Serial.println("MAX30105 was not found. Please check wiring/power. ");
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
}

void loop() {
    measureSampleJob();
}

// -- End Main Process --

// -- Setups --

void setupPreferences() {
    preferences.begin(PREF_NAMESPACE, false);

    if (preferences.getUChar(PREF_VALID_KEY, 0) != PREF_VALID_CODE) {
        Serial.println("Preferences were invalid");

        preferences.putUChar(PREF_VALID_KEY, PREF_VALID_CODE);
        preferences.putUChar(PREF_LED_BRIGHTNESS_KEY, PREF_LED_BRIGHTNESS_DEFAULT);
        preferences.putInt(PREF_INTERSECTION_POINT_KEY, PREF_INTERSECTION_POINT_DEFAULT);
        preferences.putFloat(PREF_DEVIATION_KEY, PREF_DEVIATION_DEFAULT);

        Serial.println("Preferences initialized");
    }

    if (preferences.getUChar(PREF_VALID_KEY, 0) != PREF_VALID_CODE) {
        Serial.println("Preferences cannot be initialized - using defaults");
        // Use default values instead of hanging
        ledBrightness = PREF_LED_BRIGHTNESS_DEFAULT;
        intersectionPoint = PREF_INTERSECTION_POINT_DEFAULT;
        deviation = PREF_DEVIATION_DEFAULT;
        return;  // Exit early with defaults
    }

    Serial.println("Preferences are valid");

    ledBrightness = preferences.getUChar(PREF_LED_BRIGHTNESS_KEY, PREF_LED_BRIGHTNESS_DEFAULT);
    Serial.println("Set ledBrightness to " + String(ledBrightness));

    intersectionPoint = preferences.getInt(PREF_INTERSECTION_POINT_KEY, PREF_INTERSECTION_POINT_DEFAULT);
    Serial.println("Set intersection point to " + String(intersectionPoint));

    deviation = preferences.getFloat(PREF_DEVIATION_KEY, PREF_DEVIATION_DEFAULT);
    Serial.print("Set deviation to ");
    Serial.print(deviation);
    Serial.println();
}

void setupParticleSensor() {
    
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);  // Configure sensor with these settings
    
    particleSensor.setPulseAmplitudeRed(0);
    particleSensor.setPulseAmplitudeGreen(0);

    particleSensor.disableSlots();
    particleSensor.enableSlot(2, 0x02);  // Enable only SLOT_IR_LED = 0x02
}

// -- End Setups --

// Sub Routines

void displayStartUp() {
    if (!oledAvailable) {
        Serial.println("Display: Roast Meter " + String(FIRMWARE_REVISION_STRING));
        delay(2000);
        return;
    }

    oled.clearDisplay();
#if SCREEN_HEIGHT <= 48
    oled.setCursor(4, 12 + DISPLAY_Y_OFFSET);
    oled.print("Roast Meter");
    oled.setCursor(20, 22 + DISPLAY_Y_OFFSET);
    oled.print(FIRMWARE_REVISION_STRING);
#else
    oled.setCursor(0, 0);
    oled.print("Roast  ");
    oled.print("Meter  ");
    oled.print(FIRMWARE_REVISION_STRING);
#endif
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

#if SCREEN_HEIGHT <= 48
                // 64x48 display
                oled.setTextSize(1);
                oled.setCursor(8, 8 + DISPLAY_Y_OFFSET);
                oled.println(getWarmupFace(countDownSeconds));
                oled.println();
                oled.printf(" Warm %ds", countDownSeconds);
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
#if SCREEN_HEIGHT <= 48
        oled.setTextSize(1);
        oled.setCursor(12, 8 + DISPLAY_Y_OFFSET);
        oled.println("(^o^)/");
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
        uint32_t rLevel = particleSensor.getIR();

        // Validate sensor reading
        if (rLevel == 0 || rLevel > 1000000) {  // Check for invalid readings
            Serial.println("Warning: Invalid sensor reading: " + String(rLevel));
            displayPleaseLoadSample();
            measureSampleJobTimer = millis();
            return;
        }

        long currentDelta = (long)rLevel - (long)unblockedValue;

        if (currentDelta > (long)100) {
            // Convert to smaller scale before passing to mapIRToAgtron
            uint32_t scaledLevel = rLevel / 1000;

            // Additional validation for scaled value
            if (scaledLevel > 1000) {  // Sanity check for scaled value
                Serial.println("Warning: Scaled value too high: " + String(scaledLevel));
                displayPleaseLoadSample();
                measureSampleJobTimer = millis();
                return;
            }

            int calibratedAgtronLevel = mapIRToAgtron(scaledLevel);

            // Validate Agtron result (typical range 0-350)
            if (calibratedAgtronLevel < 0 || calibratedAgtronLevel > 350) {
                Serial.println("Warning: Agtron value out of range: " + String(calibratedAgtronLevel));
                displayPleaseLoadSample();
                measureSampleJobTimer = millis();
                return;
            }

            displayMeasurement(calibratedAgtronLevel);

            Serial.println("real:" + String(rLevel));
            Serial.println("agtron:" + String(calibratedAgtronLevel));
            Serial.println("===========================");
        } else {
            displayPleaseLoadSample();
        }

        measureSampleJobTimer = millis();
    }
}

void displayPleaseLoadSample() {
    if (!oledAvailable) {
        Serial.println("Display: Please load sample!");
        return;
    }

    oled.clearDisplay();

#if SCREEN_HEIGHT <= 48
    // 64x48 (0.66" OLED)
    // Text size 1 = 8px tall, 3 lines = 24px + spacing ~30px
    // Center in 48px visible area, apply Y offset
    oled.setTextSize(1);
    oled.setCursor(4, 8 + DISPLAY_Y_OFFSET);
    oled.println("Load");
    oled.println("sample!");
#else
    // 128x64 (0.96" OLED)
    oled.setCursor(0, 0);
    oled.setTextSize(2);
    oled.println("Please ");
    oled.println("load ");
    oled.println("sample! ");
#endif

    oled.display();
}

void drawMyCenterString(const String &buf){
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    // Center horizontally and vertically
    // y1 is negative offset from cursor to top of text
    int x = (SCREEN_WIDTH - w) / 2 - x1;
    int y = (SCREEN_HEIGHT - h) / 2 - y1;
    oled.setCursor(x, y);
    oled.print(buf);
}

void displayMeasurement(int agtronLevel) {
    if (!oledAvailable) {
        Serial.println("Display: Agtron Level = " + String(agtronLevel));
        return;
    }

    oled.clearDisplay();

    String agtronLevelText = String(agtronLevel);
#if SCREEN_HEIGHT <= 48
    // 64x48 (0.66" OLED)
    oled.setTextSize(2);
    // Size 2: 12px wide per char, 16px tall
    int charWidth = 12;
    int textWidth = agtronLevelText.length() * charWidth;
    int xPos = (SCREEN_WIDTH - textWidth) / 2;
    int yPos = (SCREEN_HEIGHT - 16) / 2 + DISPLAY_Y_OFFSET;
    oled.setCursor(xPos > 0 ? xPos : 0, yPos);
    oled.print(agtronLevelText);
    // Debug output
    Serial.printf("64x48 display: x=%d y=%d (offset=%d) text='%s'\n", xPos, yPos, DISPLAY_Y_OFFSET, agtronLevelText.c_str());
#elif SCREEN_WIDTH <= 64
    // 64x64 - use size 2
    oled.setTextSize(2);
    drawMyCenterString(agtronLevelText);
#else
    // 128x64 (0.96" OLED)
    oled.setTextSize(3);
    drawMyCenterString(agtronLevelText);
#endif

    oled.display();
}


// -- End Sub Routines --

// -- Utility Functions --

int mapIRToAgtron(uint32_t x) {
    // Convert to int for calculation (x is already scaled down by /1000)
    int scaledX = (int)x;
    // Use float for intermediate calculations to avoid overflow
    float result = scaledX - (intersectionPoint - scaledX) * deviation;
    return round(result);
    //return round(intersectionPoint - (x - intersectionPoint) * deviation);
} 

// -- End Utility Functions --
