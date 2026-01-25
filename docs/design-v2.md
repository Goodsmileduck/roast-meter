# Roast Meter Case Design v2.0

**Design Revision:** 2.0  
**Date:** January 2026  
**Status:** PROPOSED

---

## 1. Design Concept

### Current Design (v1.0) - Problems

```
     ┌─────────────────┐
     │   Coffee Cup    │  ← User loads from top
     │  ▓▓▓▓▓▓▓▓▓▓▓▓▓  │
     ├─────────────────┤
     │  Polycarbonate  │
     ├─────────────────┤
     │    SENSOR       │  ← Sensor at bottom
     │    [OLED]       │
     │   [Battery]     │
     └─────────────────┘
```

**Issues:**
- Coffee grounds can fall onto sensor window
- Difficult to clean
- User has to look down into device
- Cup fills from top (awkward)
- Dust settles on polycarbonate

---

### New Design (v2.0) - Inverted

```
     ┌─────────────────────┐
     │      DEVICE         │  ← Top unit (handheld)
     │   ┌───────────┐     │
     │   │   OLED    │     │
     │   └───────────┘     │
     │    [Battery]        │
     │    [ESP32]          │
     │    [MAX30102]       │  ← Sensor faces DOWN
     │   ═══════════════   │  ← Polycarbonate window
     └─────────┬───────────┘
               │
               ▼  Place on top
     ┌─────────────────────┐
     │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │  ← Ground coffee (pressed)
     │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
     └─────────────────────┘
         SAMPLE CUP           ← Bottom unit (standalone)
```

**Benefits:**
- ✅ Sensor window faces down (stays clean)
- ✅ Easy to read OLED (faces user)
- ✅ Simple cup loading (fill, tamp, place device)
- ✅ Professional workflow (like Agtron)
- ✅ Cup is washable separately
- ✅ Multiple cups possible (batch testing)

---

## 2. Component Layout

### 2.1 Top Unit (Device)

```
        TOP VIEW
    ┌─────────────────┐
    │  ┌───────────┐  │
    │  │   OLED    │  │  128x64 or 64x48
    │  │  Display  │  │
    │  └───────────┘  │
    │                 │
    │    [Button]     │  ← Power/Mode button
    │                 │
    │  ┌───────────┐  │
    │  │  USB-C    │  │  ← Charging port
    │  └───────────┘  │
    └─────────────────┘

       BOTTOM VIEW
    ┌─────────────────┐
    │                 │
    │    ┌───────┐    │
    │    │SENSOR │    │  ← MAX30102 behind window
    │    │ ○   ○ │    │     (LEDs + photodetector)
    │    └───────┘    │
    │                 │
    │ ═══════════════ │  ← Polycarbonate window (flush)
    └─────────────────┘

       SIDE VIEW
    ┌─────────────────┐
    │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  ← Top shell
    │  [OLED]         │
    │  [ESP32]        │
    │  [Battery]      │
    │  [MAX30102]     │
    │═════════════════│  ← Window (1mm polycarbonate)
    └─────────────────┘
          │
      alignment ridge
```

### 2.2 Bottom Unit (Sample Cup)

```
        TOP VIEW
    ┌─────────────────┐
    │  ┌───────────┐  │
    │  │           │  │
    │  │  Sample   │  │  ← Ø20mm inner diameter
    │  │   Area    │  │
    │  │           │  │
    │  └───────────┘  │
    └─────────────────┘

       CROSS SECTION
    ┌─────────────────┐
    │ ┌─────────────┐ │
    │ │▓▓▓▓▓▓▓▓▓▓▓▓▓│ │  ← Coffee (10mm depth)
    │ │▓▓▓▓▓▓▓▓▓▓▓▓▓│ │
    │ └─────────────┘ │
    └─────────────────┘
      ▲             ▲
      └─ 2mm wall ──┘

    Cup outer: Ø24mm
    Cup inner: Ø20mm
    Cup depth: 10mm
    Wall thickness: 2mm
    Base thickness: 3mm
    Total height: 13mm
```

### 2.3 Assembly (Measurement Position)

```
    ┌─────────────────┐
    │    DEVICE       │
    │   [Display]     │  ← User sees reading
    │                 │
    │   [Electronics] │
    │                 │
    │   ══════════════│  ← Window contacts cup rim
    ├────────┬────────┤  ← Alignment fit
    │ ┌──────┴──────┐ │
    │ │▓▓▓▓▓▓▓▓▓▓▓▓▓│ │  ← 5mm gap (window to coffee)
    │ │▓▓ COFFEE ▓▓▓│ │
    │ └─────────────┘ │
    └─────────────────┘
         SAMPLE CUP
```

---

## 3. Detailed Dimensions

### 3.1 Device (Top Unit)

| Parameter | Dimension | Notes |
|-----------|-----------|-------|
| Length | 70mm | Fits comfortably in hand |
| Width | 45mm | |
| Height | 25mm | Including window recess |
| Window recess | 2mm deep | For cup alignment |
| Window diameter | 22mm | Slightly larger than cup inner |
| Material | PLA/PETG | 3D printed |
| Color | Black | Light blocking |

### 3.2 Sample Cup (Bottom Unit)

| Parameter | Dimension | Notes |
|-----------|-----------|-------|
| Outer diameter | 24mm | |
| Inner diameter | 20mm | Sample area |
| Sample depth | 10mm | Ground coffee |
| Wall thickness | 2mm | |
| Base thickness | 3mm | Stable standing |
| Total height | 13mm | |
| Rim height | 2mm | Fits into device recess |
| Material | PLA (black) | Light blocking, washable |

### 3.3 Optical Path

```
    Device bottom surface
    ═══════════════════════  ← Polycarbonate window (1mm)
           5mm air gap
    ───────────────────────  ← Coffee surface (pressed)
           10mm coffee
    ═══════════════════════  ← Cup bottom
```

| Parameter | Value |
|-----------|-------|
| Window thickness | 1mm |
| Window to coffee surface | 5mm |
| Total optical path | 6mm |

---

## 4. Alignment System

### Option A: Friction Fit (Simple)

```
    Device
    ┌────────────────┐
    │                │
    │    ┌──────┐    │  ← 2mm recess
    └────┤      ├────┘
         │      │
    ┌────┴──────┴────┐
    │   Sample Cup   │  ← Cup rim fits into recess
    └────────────────┘
```

### Option B: Magnetic Alignment (Premium)

```
    Device
    ┌────────[M]─────┐
    │                │  [M] = 5mm neodymium magnet
    │    ┌──────┐    │
    └────┤      ├────┘
         │      │
    ┌────┴──────┴────┐
    │      [M]       │  ← Matching magnet in cup
    └────────────────┘
```

- 4x magnets around perimeter
- Self-aligning
- Satisfying "click"
- Ensures consistent positioning

### Option C: Twist Lock (Secure)

```
    Device          Cup
    
    ┌──╮    ╭──┐    Align tabs
    │  │    │  │
    └──╯    ╰──┘
    
    ┌────────────┐   Twist 15°
    │  ══════    │
    └────────────┘
```

**Recommendation:** Start with Option A (friction fit) for MVP. Add magnets in v2 if customers want it.

---

## 5. Tamper Design

Included tamper for consistent sample preparation:

```
    ┌─────────────┐
    │   Handle    │  Height: 25mm
    │      │      │  Diameter: 25mm
    │      │      │
    ├──────┴──────┤
    │  ▓▓▓▓▓▓▓▓▓  │  Tamper head
    └─────────────┘  Diameter: 19mm (1mm clearance)
                     Flat bottom
```

### Tamper Dimensions

| Parameter | Value |
|-----------|-------|
| Handle diameter | 25mm |
| Handle height | 25mm |
| Head diameter | 19mm |
| Head height | 5mm |
| Total height | 30mm |
| Material | PLA/PETG |

---

## 6. Internal Component Layout

### 6.1 Exploded View

```
    Layer 1 (Top):     OLED Display
                       ↓
    Layer 2:           ESP32-S3 Mini
                       ↓
    Layer 3:           LiPo Battery (3.7V 500mAh)
                       ↓
    Layer 4:           TP4056 Charger + Button
                       ↓
    Layer 5 (Bottom):  MAX30102 Sensor
                       ↓
    Layer 6:           Polycarbonate Window
```

### 6.2 PCB/Wiring Layout

```
    ┌─────────────────────────────────────┐
    │  ┌─────────┐                        │
    │  │  OLED   │  ← I2C (SDA, SCL)      │
    │  └────┬────┘                        │
    │       │                             │
    │  ┌────┴────┐     ┌──────┐          │
    │  │ ESP32-S3│─────│TP4056│──[USB-C] │
    │  │  Mini   │     └──┬───┘          │
    │  └────┬────┘        │              │
    │       │        ┌────┴────┐         │
    │       │        │ Battery │         │
    │       │        │ 500mAh  │         │
    │       │        └─────────┘         │
    │  ┌────┴────┐                       │
    │  │MAX30102 │  ← I2C (same bus)     │
    │  └─────────┘                       │
    │  ═══════════  ← Window             │
    └─────────────────────────────────────┘
```

### 6.3 I2C Bus

| Device | Address | Notes |
|--------|---------|-------|
| OLED SSD1306 | 0x3C | Display |
| MAX30102 | 0x57 | Sensor |

Both on same I2C bus - no conflicts.

---

## 7. Material Specifications

### 7.1 Device Shell

| Part | Material | Color | Notes |
|------|----------|-------|-------|
| Top shell | PETG | Black | Durable, heat resistant |
| Bottom shell | PETG | Black | Light blocking |
| Window | Polycarbonate | Clear | 1mm, optical grade |

### 7.2 Sample Cup

| Part | Material | Color | Notes |
|------|----------|-------|-------|
| Cup body | PLA | Black | Food-safe, washable |
| Alternative | Aluminum | Black anodized | Premium version |

### 7.3 Tamper

| Part | Material | Color |
|------|----------|-------|
| Body | PLA | Black or wood color |

---

## 8. Manufacturing Notes

### 8.1 3D Printing Settings

**Device Shell:**
```
Layer height: 0.2mm
Infill: 30%
Walls: 3
Top/Bottom layers: 4
Material: PETG
Supports: Yes (for window recess)
Print orientation: Top shell face-down
```

**Sample Cup:**
```
Layer height: 0.16mm (smoother finish)
Infill: 50%
Walls: 3
Top/Bottom layers: 5
Material: PLA
Supports: No
Print orientation: Base down
```

### 8.2 Post-Processing

1. **Window installation:**
   - Cut polycarbonate circle (22mm diameter)
   - Clean with IPA
   - Glue with optical adhesive or silicone
   - Ensure no air bubbles

2. **Light seal check:**
   - Assemble device
   - Shine flashlight at seams
   - No light should leak into sensor chamber

3. **Cup finishing:**
   - Light sanding of rim (smooth contact surface)
   - Optional: food-safe coating

---

## 9. Bill of Materials (Case Only)

| Item | Qty | Unit Cost | Total |
|------|-----|-----------|-------|
| PETG filament (device) | 30g | $0.02/g | $0.60 |
| PLA filament (cup + tamper) | 15g | $0.02/g | $0.30 |
| Polycarbonate sheet 1mm | 1pc | $0.20 | $0.20 |
| M2x5 screws | 4 | $0.02 | $0.08 |
| Silicone adhesive | 1g | $0.05 | $0.05 |
| **Total** | | | **$1.23** |

### With Magnets (Option B)

| Item | Qty | Unit Cost | Total |
|------|-----|-----------|-------|
| Above total | - | - | $1.23 |
| 5mm neodymium magnets | 8 | $0.10 | $0.80 |
| **Total** | | | **$2.03** |

---

## 10. Assembly Instructions

### Step 1: Prepare Components

- [ ] 3D print: top shell, bottom shell, cup, tamper
- [ ] Cut polycarbonate window (22mm circle)
- [ ] Gather: ESP32, MAX30102, OLED, battery, TP4056

### Step 2: Install Window

1. Clean window recess in bottom shell
2. Apply thin bead of silicone around edge
3. Press polycarbonate into recess
4. Ensure flush and sealed
5. Cure 24 hours

### Step 3: Install Sensor

1. Position MAX30102 above window (sensor side down)
2. Ensure LEDs/photodetector centered over window
3. Secure with hot glue or mounting tape
4. Leave 2-3mm gap between sensor and window

### Step 4: Wire Electronics

```
ESP32 → MAX30102:
  3.3V → VIN
  GND  → GND
  SDA  → SDA (GPIO21)
  SCL  → SCL (GPIO22)

ESP32 → OLED:
  3.3V → VCC
  GND  → GND
  SDA  → SDA (shared)
  SCL  → SCL (shared)

ESP32 → TP4056:
  5V   → OUT+
  GND  → OUT-

Battery → TP4056:
  BAT+ → B+
  BAT- → B-
```

### Step 5: Final Assembly

1. Place OLED in top shell window
2. Arrange ESP32 and battery
3. Route wires cleanly
4. Close shells
5. Secure with screws

### Step 6: Test

1. Power on
2. Run `TEST` command via serial
3. Verify sensor reads through window
4. Check display visibility
5. Test cup alignment

---

## 11. User Manual (Quick Start)

### How to Use

```
1. PREPARE SAMPLE
   ┌─────────┐
   │ ▓▓▓▓▓▓▓ │  Fill cup with ground coffee
   └─────────┘

2. TAMP
   ┌───┴───┐
   │▓▓▓▓▓▓▓│  Press lightly with tamper
   └───────┘

3. MEASURE
   ┌─────────┐
   │ DEVICE  │  Place device on cup
   ├─────────┤
   │▓▓▓▓▓▓▓▓▓│
   └─────────┘

4. READ
   ┌─────────┐
   │   65    │  Read Agtron value
   └─────────┘

5. CLEAN
   Wipe cup, repeat
```

---

## 12. Design Files Checklist

- [ ] `device_top_shell.stl`
- [ ] `device_bottom_shell.stl`
- [ ] `sample_cup.stl`
- [ ] `tamper.stl`
- [ ] `window_template.dxf` (laser cutting)
- [ ] Assembly drawing (PDF)
- [ ] BOM spreadsheet

---

## 13. Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Dec 2025 | Initial design (sensor bottom) |
| 2.0 | Jan 2026 | Inverted design (sensor top) |

---

## Appendix A: Comparison with Competitors

| Feature | Our Design | Tonino | Lighttells |
|---------|------------|--------|------------|
| Sensor position | Top (device) | Top | Top |
| Sample cup | Separate | Built-in tray | Separate |
| Alignment | Friction/magnet | Slot | Friction |
| Display | Top of device | Front | Front |
| Tamper | Included | Not included | Optional |

**Our design follows professional meter conventions.**

---

## Appendix B: Future Enhancements

### v2.1 Ideas

1. **Multiple cup sizes** (espresso vs cupping grind)
2. **Cup with built-in tamper** (twist mechanism)
3. **Lanyard attachment point**
4. **Protective carrying case**
5. **Aluminum cup option** (premium)
6. **Calibration tile holder**

---

**Document End**
