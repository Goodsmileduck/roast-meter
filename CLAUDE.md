# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Roast Meter is an ESP32-based DIY coffee roast color meter that measures bean reflectance and converts it to an Agtron-scale reading (0-350). The device uses a MAX30105 IR optical sensor and SSD1306 OLED display.

## Build Commands

```bash
# Build firmware
pio run -e lolin_s2_mini

# Upload to device
pio run -e lolin_s2_mini -t upload

# Monitor serial output (115200 baud)
pio device monitor
```

## Architecture

Single-file firmware (`src/roast_meter.cpp`) with this flow:

1. **Initialization**: I2C bus, OLED display, preferences from NVS, MAX30105 sensor
2. **Warmup**: 60-second LED stabilization period
3. **Measurement loop**: Continuous IR sampling at 100ms intervals, converts readings to Agtron scale

### Key Components

- **Sensor**: MAX30105 configured for IR-only mode (slot 2, 0x02)
- **Display**: SSD1306 128x64 OLED at I2C address 0x3C
- **Storage**: ESP32 NVS (Preferences library) with namespace `"roast_meter"`

### Calibration Formula

```cpp
agtronLevel = scaledIR - (intersectionPoint - scaledIR) * deviation
```

Where `scaledIR = rawIR / 1000`. Calibration parameters stored in NVS:
- `led_brightness` (default: 95)
- `intersection_point` (default: 117)
- `deviation` (default: 0.165)

## Hardware

- Board: LOLIN S2 Mini (ESP32-S2)
- Sensor: MAX30105 IR optical sensor
- Display: SSD1306 128x64 OLED
- Interface: I2C (Wire library)
