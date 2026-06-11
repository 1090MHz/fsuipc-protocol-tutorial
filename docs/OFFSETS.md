# FSUIPC Offset Reference

> **Complete documentation of implemented FSUIPC offsets with encoding specifications**

This document provides detailed specifications for FSUIPC offsets implemented in this educational server, including data types, encodings, and example conversions.

> **Note:** These are standard FSUIPC offset addresses used across all FSUIPC versions. The offset system has been consistent since the original FSUIPC.

## Table of Contents

1. [Understanding FSUIPC Offsets](#understanding-fsuipc-offsets)
2. [Implemented Offsets](#implemented-offsets)
3. [Encoding Reference](#encoding-reference)
4. [Common Patterns](#common-patterns)
5. [Adding New Offsets](#adding-new-offsets)

---

## Understanding FSUIPC Offsets

### What is an Offset?

An **offset** is a byte address in FSUIPC's 64KB memory space where flight simulator data is stored. Think of it like an array:

```cpp
uint8_t fsuipc_memory[65536];  // 64KB = 0x0000 to 0xFFFF

// Offset 0x0238 = heading (4 bytes starting at byte 0x0238)
uint32_t heading_raw = *reinterpret_cast<uint32_t*>(&fsuipc_memory[0x0238]);
```

### Why Encoded Values?

FSUIPC stores values in **scaled integer formats** rather than floating-point:

✅ **Advantages:**

- Fixed binary layout (no floating-point representation differences)
- Atomic reads/writes (single memory operation)
- Efficient for low-level programming (assembly, VB6, etc.)

Example:

```
Heading 270.5°:
- As float:  0x4387 4000 (may vary between systems)
- As FSUIPC: 0x0001 1F00 (always identical, easy bit manipulation)
```

### Data Types

| Type    | Size | Range                           | Signed |
| ------- | ---- | ------------------------------- | ------ |
| `BYTE`  | 1    | 0-255                           | No     |
| `WORD`  | 2    | 0-65535                         | No     |
| `DWORD` | 4    | 0-4,294,967,295                 | No     |
| `QWORD` | 8    | 0-18,446,744,073,709,551,615    | No     |
| `short` | 2    | -32,768 to 32,767               | Yes    |
| `long`  | 4    | -2,147,483,648 to 2,147,483,647 | Yes    |
| `int64` | 8    | ±9,223,372,036,854,775,807      | Yes    |

---

## Implemented Offsets

### Internal / Validation Offsets

#### 0x3304 - FSUIPC Version (DWORD, Read-Only)

**Purpose:** Identify FSUIPC version for compatibility checking

**Encoding:**

```
HIWORD = version × 1000
LOWORD = build number
```

**Example:**

```cpp
uint32_t version = 0x70000001;

uint16_t major_minor = version >> 16;  // 0x7000 = 28672
double version_num = major_minor / 1000.0;  // 28.672 = version 7.0

uint16_t build = version & 0xFFFF;  // 0x0001 = build 1
```

**Decoding Function:**

```cpp
std::string DecodeFSUIPCVersion(uint32_t raw) {
    uint16_t ver = (raw >> 16);
    uint16_t build = (raw & 0xFFFF);

    char buf[32];
    sprintf(buf, "%.3f build %d", ver / 1000.0, build);
    return buf;
}
```

**Test Value:** `0x70000001` = FSUIPC 7.000 build 1

---

#### 0x3308 - FS Version / Validity Token (DWORD, Read-Only)

**Purpose:** Identify flight simulator type and validate FSUIPC connection

**Encoding:**

```
HIWORD = 0xFADE (validity token)
LOWORD = FS type
```

**FS Type Values:**

- `1` = FS98
- `2` = FS2000
- `3` = CFS2
- `4` = CFS1
- `5` = FLY!
- `6` = FS2002
- `7` = FS2004
- `8` = FSX
- `9` = ESP / P3D / MSFS (generic)

**Example:**

```cpp
uint32_t fs_version = 0xFADE0009;

uint16_t token = fs_version >> 16;  // 0xFADE
if (token != 0xFADE) {
    // Not a valid FSUIPC server!
}

uint16_t fs_type = fs_version & 0xFFFF;  // 0x0009 = modern simulator
```

**Test Value:** `0xFADE0009` = Modern simulator (MSFS/P3D/X-Plane)

---

### Flight Position & Attitude

#### 0x0238 - Magnetic Heading (DWORD)

**Purpose:** Aircraft magnetic heading (compass direction)

**Encoding:** `degrees × 65536 / 360`

**Range:** 0-359.999° → 0-65535

**Decoding:**

```cpp
double DecodeHeading(uint32_t raw) {
    return (raw * 360.0) / 65536.0;
}
```

**Encoding:**

```cpp
uint32_t EncodeHeading(double degrees) {
    // Normalize to 0-360
    while (degrees < 0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;

    return static_cast<uint32_t>(degrees * 65536.0 / 360.0);
}
```

**Examples:**
| Heading | Raw Value | Calculation |
|---------|-----------|-------------|
| 0.0° | `0x00000000` | 0 × 65536 / 360 = 0 |
| 90.0° | `0x00004000` | 90 × 65536 / 360 = 16384 |
| 180.0° | `0x00008000` | 180 × 65536 / 360 = 32768 |
| 270.0° | `0x0000C000` | 270 × 65536 / 360 = 49152 |
| 359.9° | `0x0000FFEE` | 359.9 × 65536 / 360 ≈ 65518 |

**Precision:** ~0.0055° per increment

---

#### 0x0560 - Latitude (QWORD, Signed)

**Purpose:** Aircraft latitude position

**Encoding:** `degrees × (10001750.0 * 65536.0 * 65536.0) / 90.0`

**Range:** -90.0° to +90.0°

**Decoding:**

```cpp
double DecodeLatitude(int64_t raw) {
    return raw * 90.0 / (10001750.0 * 65536.0 * 65536.0);
}
```

**Encoding:**

```cpp
int64_t EncodeLatitude(double degrees) {
    return static_cast<int64_t>(
        degrees * (10001750.0 * 65536.0 * 65536.0) / 90.0
    );
}
```

**Examples:**
| Latitude | Raw Value | Notes |
|----------|-----------|-------|
| 0.0° (Equator) | `0x0000000000000000` | Zero |
| +51.4770° (London) | `0x000000B123456789` | Positive = North |
| -33.9461° (Sydney) | `0xFFFFFF4EDCBA9876` | Negative = South |
| +90.0° (North Pole) | `0x4E47FD8B54A3F800` | Maximum |
| -90.0° (South Pole) | `0xB1B8027A4AB5C800` | Minimum |

**Precision:** ~0.000000006° (~0.7mm)

---

#### 0x0568 - Longitude (QWORD, Signed)

**Purpose:** Aircraft longitude position

**Encoding:** `degrees × INT64_MAX / 180.0`

**Range:** -180.0° to +180.0°

**Decoding:**

```cpp
double DecodeLongitude(int64_t raw) {
    return raw * 180.0 / static_cast<double>(INT64_MAX);
}
```

**Encoding:**

```cpp
int64_t EncodeLongitude(double degrees) {
    // Normalize to -180 to +180
    while (degrees < -180.0) degrees += 360.0;
    while (degrees > 180.0) degrees -= 360.0;

    return static_cast<int64_t>(
        degrees * static_cast<double>(INT64_MAX) / 180.0
    );
}
```

**Examples:**
| Longitude | Raw Value | Notes |
|-----------|-----------|-------|
| 0.0° (Prime Meridian) | `0x0000000000000000` | Greenwich |
| -0.4610° (London) | `0xFFFFFFFFFFE12345` | West (negative) |
| +151.2099° (Sydney) | `0x0012345678ABCDEF` | East (positive) |
| +180.0° (Date Line) | `0x7FFFFFFFFFFFFFFF` | Maximum |
| -180.0° (Date Line) | `0x8000000000000000` | Minimum |

**Precision:** ~0.000000004° (~0.4mm)

---

#### 0x0570 - Altitude MSL (QWORD, Signed)

**Purpose:** Aircraft altitude above mean sea level

**Encoding:** `meters × 65536`

**Range:** -1,500m to +100,000m (approx)

**Decoding:**

```cpp
double DecodeAltitudeFeet(int64_t raw) {
    double meters = raw / 65536.0;
    return meters * 3.28084;  // Convert to feet
}

double DecodeAltitudeMeters(int64_t raw) {
    return raw / 65536.0;
}
```

**Encoding:**

```cpp
int64_t EncodeAltitude(double meters) {
    return static_cast<int64_t>(meters * 65536.0);
}
```

**Examples:**
| Altitude | Meters | Raw Value | Notes |
|----------|--------|-----------|-------|
| Sea level | 0m | `0x0000000000000000` | Zero |
| FL100 | 3,048m | `0x0000000000BF0000` | 10,000 ft |
| FL340 | 10,363m | `0x0000000028760000` | Cruise altitude |
| Everest | 8,848m | `0x0000000022C00000` | 29,029 ft |

**Precision:** ~0.000015m (~0.05mm)

---

#### 0x0578 - Pitch Angle (DWORD, Signed)

**Purpose:** Aircraft pitch attitude (nose up/down)

**Encoding:** `degrees × 65536 × 65536 / 360`

**Range:** -90° to +90° (typically -20° to +30° in normal flight)

**Decoding:**

```cpp
double DecodePitch(int32_t raw) {
    return (raw * 360.0) / (65536.0 * 65536.0);
}
```

**Encoding:**

```cpp
int32_t EncodePitch(double degrees) {
    return static_cast<int32_t>(
        degrees * (65536.0 * 65536.0) / 360.0
    );
}
```

**Examples:**
| Pitch | Raw Value | Notes |
|-------|-----------|-------|
| 0.0° (Level) | `0x00000000` | Horizon |
| +5.0° (Climb) | `0x05A00000` | Typical climb |
| -3.0° (Descent) | `0xFCA60000` | Typical descent |
| +15.0° (Steep climb) | `0x10F00000` | Takeoff |

**Precision:** ~0.0000084° (~0.03 arc-seconds)

---

### Speed & Velocity

#### 0x02B4 - Ground Speed (DWORD)

**Purpose:** Aircraft speed over ground

**Encoding:** `meters_per_second × 65536`

**Range:** 0-1,000+ m/s (0-1,944+ knots)

**Decoding:**

```cpp
double DecodeGroundSpeedKnots(uint32_t raw) {
    double mps = raw / 65536.0;
    return mps / 0.514444;  // m/s → knots
}

double DecodeGroundSpeedMPS(uint32_t raw) {
    return raw / 65536.0;
}
```

**Encoding:**

```cpp
uint32_t EncodeGroundSpeed(double mps) {
    return static_cast<uint32_t>(mps * 65536.0);
}
```

**Examples:**
| Speed | m/s | Raw Value | Knots |
|-------|-----|-----------|-------|
| Stopped | 0 | `0x00000000` | 0 |
| Taxi | 5 | `0x00050000` | 9.7 |
| Cruise (B737) | 240 | `0x00F00000` | 466.5 |
| Supersonic | 600 | `0x02580000` | 1,166 |

**Precision:** ~0.000015 m/s (~0.03 knots)

---

#### 0x02BC - Indicated Airspeed (DWORD)

**Purpose:** Airspeed as shown on cockpit instruments

**Encoding:** `knots × 128`

**Range:** 0-2,000+ knots

**Decoding:**

```cpp
double DecodeIAS(uint32_t raw) {
    return raw / 128.0;
}
```

**Encoding:**

```cpp
uint32_t EncodeIAS(double knots) {
    return static_cast<uint32_t>(knots * 128.0);
}
```

**Examples:**
| IAS | Raw Value | Notes |
|-----|-----------|-------|
| 0 kts | `0x00000000` | Stopped |
| 60 kts | `0x00001E00` | Rotation speed (small GA) |
| 150 kts | `0x00004B00` | Cruise (C172) |
| 250 kts | `0x00007D00` | Below 10,000 ft limit |
| 450 kts | `0x0000E100` | Jet cruise |

**Precision:** ~0.0078 knots

---

#### 0x02C8 - Vertical Speed (DWORD, Signed)

**Purpose:** Rate of climb/descent

**Encoding:** `meters_per_second × 256`

**Range:** -100 to +100 m/s (approx -20,000 to +20,000 fpm)

**Decoding:**

```cpp
double DecodeVSFPM(int32_t raw) {
    double mps = raw / 256.0;
    return mps / 0.00508;  // m/s → feet per minute
}

double DecodeVSMPS(int32_t raw) {
    return raw / 256.0;
}
```

**Encoding:**

```cpp
int32_t EncodeVS(double mps) {
    return static_cast<int32_t>(mps * 256.0);
}
```

**Examples:**
| VS | m/s | Raw Value | FPM | Notes |
|----|-----|-----------|-----|-------|
| Level | 0 | `0x00000000` | 0 | Cruise |
| Gentle climb | +2.5 | `0x00000280` | +492 | Normal climb |
| Steep climb | +10.0 | `0x00000A00` | +1,969 | After takeoff |
| Descent | -5.0 | `0xFFFFFB00` | -984 | Approach |
| Emergency descent | -50.0 | `0xFFFF9C00` | -9,843 | Rapid |

**Precision:** ~0.0039 m/s (~0.77 fpm)

---

### Aircraft Systems

#### 0x0330 - Altimeter Pressure Setting (WORD)

**Purpose:** Barometric pressure (QNH) set on altimeter

**Encoding:** `millibars × 16`

**Range:** 900-1100 mb (26.58-32.48 inHg)

**Decoding:**

```cpp
double DecodeBaroMB(uint16_t raw) {
    return raw / 16.0;
}

double DecodeBaroInHg(uint16_t raw) {
    double mb = raw / 16.0;
    return mb * 0.02953;  // mb → inches of mercury
}
```

**Encoding:**

```cpp
uint16_t EncodeBaro(double millibars) {
    return static_cast<uint16_t>(millibars * 16.0);
}
```

**Examples:**
| Pressure | Raw Value | inHg | Notes |
|----------|-----------|------|-------|
| 1013.25 mb | `0x3F54` | 29.92 | ISA standard |
| 1020.00 mb | `0x3FC0` | 30.12 | High pressure |
| 1000.00 mb | `0x3E80` | 29.53 | Low pressure |
| 950.00 mb | `0x3B60` | 28.05 | Hurricane |

**Precision:** 0.0625 mb (~0.002 inHg)

---

#### 0x0366 - On Ground Flag (WORD)

**Purpose:** Aircraft on ground vs airborne

**Encoding:** Boolean (0 or 1)

**Values:**

- `0x0000` = Airborne
- `0x0001` = On ground

**Decoding:**

```cpp
bool IsOnGround(uint16_t raw) {
    return raw != 0;
}
```

**Example:**

```cpp
uint16_t flag = ReadOffset<uint16_t>(0x0366);

if (flag == 0) {
    std::cout << "Airborne\n";
} else {
    std::cout << "On ground\n";
}
```

---

## Encoding Reference

### Common Encoding Patterns

#### Fixed-Point Encoding

```
raw_value = real_value × scale_factor
```

**Examples:**

- Heading: `scale = 65536 / 360 = 182.044`
- IAS: `scale = 128`
- Altitude: `scale = 65536`

#### Decoding Formula

```
real_value = raw_value / scale_factor
```

#### Preserving Precision

Always use **double** for intermediate calculations:

```cpp
// ❌ BAD: Loss of precision
uint32_t raw = 49152;
float heading = (raw * 360) / 65536;  // Integer overflow!

// ✅ GOOD: Use floating-point
uint32_t raw = 49152;
double heading = (raw * 360.0) / 65536.0;  // Correct
```

---

## Common Patterns

### Reading Multiple Offsets

```cpp
// Batch read in one IPC call
QueueRead(0x0238, 4, &heading_raw);
QueueRead(0x02B4, 4, &gs_raw);
QueueRead(0x02BC, 4, &ias_raw);
Process();  // Single IPC call

// Decode all
double heading = DecodeHeading(heading_raw);
double gs_kts = DecodeGroundSpeedKnots(gs_raw);
double ias_kts = DecodeIAS(ias_raw);
```

### Writing Offsets

```cpp
// Encode
uint16_t baro_raw = EncodeBaro(1013.25);

// Write
QueueWrite(0x0330, 2, &baro_raw);
Process();
```

---

## Adding New Offsets

### Step 1: Add to Server

**In `fsuipc_server.cpp`:**

```cpp
// Add to FlushSimState()
void FlushSimState(const SimState& s) {
    // ... existing code ...

    // New offset: 0x0BC0 - Rudder position
    int16_t rudder_raw = static_cast<int16_t>(s.rudder_pct * 16384.0);
    WriteOff(0x0BC0, rudder_raw);
}
```

### Step 2: Add to Simulator State

```cpp
struct SimState {
    // ... existing fields ...
    double rudder_pct = 0.0;  // -1.0 (left) to +1.0 (right)
};
```

### Step 3: Update in Simulator Thread

```cpp
void SimulatorThread() {
    SimState s;
    while (g_Running) {
        // ... existing updates ...

        s.rudder_pct = 0.1 * sin(elapsed_time * 0.5);  // Oscillate

        FlushSimState(s);
        Sleep(500);
    }
}
```

### Step 4: Add Decoding Function

```cpp
// Rudder: -16384 (full left) to +16384 (full right)
double DecodeRudder(int16_t raw) {
    return raw / 16384.0;  // -1.0 to +1.0
}
```

### Step 5: Test in Client

```cpp
int16_t rudder_raw = 0;
QueueRead(0x0BC0, 2, &rudder_raw);
Process();

double rudder_pct = DecodeRudder(rudder_raw);
std::cout << "Rudder: " << (rudder_pct * 100.0) << "%\n";
```

---

## Offset Ranges

### Memory Map Overview

| Range         | Purpose                     |
| ------------- | --------------------------- |
| 0x0000-0x0FFF | Basic aircraft data         |
| 0x1000-0x1FFF | Radios, navigation          |
| 0x2000-0x2FFF | Engine parameters           |
| 0x3000-0x3FFF | System status, FSUIPC info  |
| 0x4000-0x4FFF | Miscellaneous               |
| 0x5000-0x5FFF | Extended positions (64-bit) |
| 0x6000-0xEFFF | Various systems             |
| 0xF000-0xFFFF | TCAS/AI traffic data        |

---

## References

- [Official FSUIPC Offsets](http://fsuipcoffsets.com/) - Complete list
- [Pete Dowson's Documentation](http://www.schiratti.com/dowson.html) - Original FSUIPC docs
- [FSUIPC SDK](http://www.schiratti.com/dowson.html) - Headers and examples

---

**Questions about encodings?** Open an issue with:

- Offset number
- What value you're trying to encode/decode
- Your calculation attempt

The community is here to help!
