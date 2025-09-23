// VERSION 1.0.0
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "MAX30105.h"

// -- Constant Values --
#define FIRMWARE_REVISION_STRING "v0.2"

// #define PIN_RESET 9
// #define DC_JUMPER 1


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

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_ADDRESS_OLED 0x3C

// -- End Preferences constants

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

String multiplyChar(char c, int n);
String stringLastN(String input, int n);
int mapIRToAgtron(uint32_t x);

// -- End Utility Function Headers --

// -- Main Process --
void setup() {
    Serial.begin(115200);

    Wire.begin();

    // Initialize OLED
    if (!oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS_OLED)) {
        Serial.println(F("❌ OLED initialization failed!"));
        // Continue without display - device can still work via serial
        Serial.println(F("Continuing without display..."));
        oledAvailable = false;
    } else {
        oledAvailable = true;
        Serial.println(F("✅ OLED initialized successfully"));
    }

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(WHITE);
    oled.setCursor(0, 0);
    oled.println("Initializing...");
    oled.display();

    setupPreferences();

    // Initialize sensor
    if (particleSensor.begin(Wire, 400000) == false)  // Use default I2C port, 400kHz speed
    {
        Serial.println("MAX30105 was not found. Please check wiring/power. ");
        oled.clearDisplay();
        oled.setCursor(0, 0);
        oled.println("Sensor Error!");
        oled.println("Check wiring");
        oled.display();

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

    oled.setCursor(0, 0);
    oled.clearDisplay();
    oled.print("Roast  ");
    oled.print("Meter  ");
    oled.print(FIRMWARE_REVISION_STRING);
    oled.display();

    delay(2000);
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
                oled.setCursor(0, 2);
                oled.setTextSize(1);
                oled.printf("Warm Up %ds", countDownSeconds);
                oled.display();
            } else {
                Serial.println("Warm Up " + String(countDownSeconds) + "s");
            }

            jobTimer = millis();
        }
    }
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
    oled.setCursor(0, 0);
    oled.setTextSize(2);

    oled.println("Please ");
    oled.println("load ");
    oled.println("sample! ");
    oled.display();
}

void drawMyCenterString(const String &buf, int x, int y){
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    oled.setCursor( (oled.width() - w)/2, y);
    oled.print(buf);
}

void displayMeasurement(int agtronLevel) {
    if (!oledAvailable) {
        Serial.println("Display: Agtron Level = " + String(agtronLevel));
        return;
    }

    oled.clearDisplay();
    //oled.setCursor(20, 25);
    oled.setTextSize(3);

    String agtronLevelText = String(agtronLevel);
    drawMyCenterString(agtronLevelText, 0, 20);

    oled.display();
}


// -- End Sub Routines --

// -- Utility Functions --

String multiplyChar(char c, int n) {
    String result;
    result.reserve(n);  // Pre-allocate memory to avoid fragmentation
    for (int i = 0; i < n; i++) {
        result += c;
    }
    return result;
}

String stringLastN(String input, int n) {
    int inputSize = input.length();

    return (n > 0 && inputSize > n) ? input.substring(inputSize - n) : "";
}

int mapIRToAgtron(uint32_t x) {
    // Convert to int for calculation (x is already scaled down by /1000)
    int scaledX = (int)x;
    // Use float for intermediate calculations to avoid overflow
    float result = scaledX - (intersectionPoint - scaledX) * deviation;
    return round(result);
    //return round(intersectionPoint - (x - intersectionPoint) * deviation);
} 

// -- End Utility Functions --
