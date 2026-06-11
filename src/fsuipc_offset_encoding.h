/**
 * @file    fsuipc_offset_encoding.h
 * @brief   FSUIPC offset encoding infrastructure (state, accessors, encoders)
 *
 * This header contains the low-level encoding helpers that convert simulation
 * data into FSUIPC's binary wire format. By separating encoders from the offset
 * table, we gain:
 *
 *   ✅ Testability: Can unit-test each encoder independently
 *   ✅ Reusability: Can use encoders in other contexts (logging, validation)
 *   ✅ Clarity: Clean separation between "what data" (SimState) and "how to encode"
 *   ✅ Modularity: Encoders can be changed without touching offset table
 *
 * TESTING EXAMPLE:
 * ────────────────
 *   // Test heading encoder
 *   assert(EncodeHeading(0.0)   == 0);        // North = 0
 *   assert(EncodeHeading(90.0)  == 16384);    // East = 16384
 *   assert(EncodeHeading(270.0) == 49152);    // West = 49152
 *   assert(EncodeHeading(360.0) == 0);        // Wraps around
 *
 * PERFORMANCE:
 * ────────────
 * All functions are marked `inline` and `noexcept` for zero-overhead calls.
 * At 60Hz × 1000 offsets = 60,000 encoder calls/sec, every nanosecond counts!
 */

#pragma once
#ifndef FSUIPC_OFFSET_ENCODING_H
#define FSUIPC_OFFSET_ENCODING_H

#include "fsuipc_ipc.h"

#include <cstdint>
#include <cstring>
#include <cmath>

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Simulation state structure
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  All mutable flight-data parameters for the simulated aircraft.
 *
 * This structure holds the "source data" that gets encoded into FSUIPC offsets.
 * In a real integration (e.g., X-Plane plugin), this would be replaced with
 * direct DataRef reads instead of a struct.
 */
struct SimState {
    double heading_deg  = 270.0;    ///< Magnetic heading (degrees, W = 270)
    double ias_kts      = 445.0;    ///< Indicated airspeed (knots)
    double gs_mps       = 240.0;    ///< Ground speed (m/s ≈ 467 kts)
    double vs_mps       = 2.54;     ///< Vertical speed (m/s ≈ 500 fpm climb)
    double alt_m        = 10363.0;  ///< Altitude MSL (m ≈ 34,000 ft, climbing)
    double lat_deg      =  51.477;  ///< Latitude  (London Heathrow area, °N)
    double lon_deg      =  -0.461;  ///< Longitude (London Heathrow area, °W)
    double pitch_deg    =   1.5;    ///< Pitch (°, positive = nose up)
    double baro_mb      = 1013.25;  ///< Altimeter setting (mb, ISA standard)
    bool   on_ground    =   false;  ///< On-ground flag
};

// ─────────────────────────────────────────────────────────────────────────────
// Offset memory accessors (inline for performance)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Write a typed value into offset memory at a given FSUIPC offset.
 *
 * USAGE:
 *   WriteOffset<uint32_t>(buffer, 0x0238, EncodeHeading(270.0));
 *   WriteOffset<int64_t>(buffer, 0x0560, EncodeLatitude(51.477));
 */
template<typename T>
static inline void WriteOffset(uint8_t* base, uint32_t offset, const T& val) noexcept {
    std::memcpy(base + offset, &val, sizeof(T));
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoding helper functions (FSUIPC wire format conversions)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Encode magnetic heading into FSUIPC format.
 * @param  deg  Heading in degrees [0, 360).
 * @return Raw DWORD value (deg × 65536 / 360).
 *
 * EXAMPLES:
 *   0° (N)   → 0
 *   90° (E)  → 16384
 *   180° (S) → 32768
 *   270° (W) → 49152
 */
static inline uint32_t EncodeHeading(double deg) noexcept {
    return static_cast<uint32_t>(fmod(deg, 360.0) * 65536.0 / 360.0);
}

/**
 * @brief  Encode pitch angle into FSUIPC format.
 * @param  deg  Pitch in degrees (positive = nose up).
 * @return Raw int32 value (deg × 65536² / 360, signed).
 *
 * RANGE:
 *   +90° (vertical up)   → +1,073,741,824
 *    0° (level)          → 0
 *   -90° (vertical down) → -1,073,741,824
 */
static inline int32_t EncodePitch(double deg) noexcept {
    return static_cast<int32_t>(deg * 65536.0 * 65536.0 / 360.0);
}

/**
 * @brief  Encode ground speed into FSUIPC format.
 * @param  mps  Ground speed in metres per second.
 * @return Raw DWORD value (m/s × 65536).
 *
 * EXAMPLES:
 *   100 m/s (≈194 kts) → 6,553,600
 *   240 m/s (≈467 kts) → 15,728,640
 */
static inline uint32_t EncodeGroundSpeed(double mps) noexcept {
    return static_cast<uint32_t>(mps * 65536.0);
}

/**
 * @brief  Encode indicated airspeed into FSUIPC format.
 * @param  kts  Indicated airspeed in knots.
 * @return Raw DWORD value (kts × 128).
 *
 * EXAMPLES:
 *   150 kts → 19,200
 *   445 kts → 56,960
 */
static inline uint32_t EncodeIAS(double kts) noexcept {
    return static_cast<uint32_t>(kts * 128.0);
}

/**
 * @brief  Encode vertical speed into FSUIPC format.
 * @param  mps  Vertical speed in metres per second (positive = climb).
 * @return Raw int32 value (m/s × 256, signed).
 *
 * EXAMPLES:
 *   +2.54 m/s (≈500 fpm climb)   → +650
 *   0 m/s (level)                 → 0
 *   -5.08 m/s (≈1000 fpm descent) → -1,300
 */
static inline int32_t EncodeVerticalSpeed(double mps) noexcept {
    return static_cast<int32_t>(mps * 256.0);
}

/**
 * @brief  Encode barometric pressure into FSUIPC format.
 * @param  mb  Pressure in millibars (hectopascals).
 * @return Raw WORD value (mb × 16).
 *
 * EXAMPLES:
 *   1013.25 mb (ISA standard) → 16,212
 *   1000 mb                   → 16,000
 *   1030 mb                   → 16,480
 */
static inline uint16_t EncodeBarometer(double mb) noexcept {
    return static_cast<uint16_t>(mb * 16.0);
}

/**
 * @brief  Encode latitude into FSUIPC format.
 * @param  deg  Latitude in decimal degrees (positive = North).
 * @return Raw signed QWORD value (deg × 10001750/90 × 65536²).
 *
 * DERIVATION:
 *   Formula comes from FSUIPC SDK documentation:
 *     raw = lat_deg × (10001750.0 / 90.0) × 65536²
 *
 * EXAMPLES:
 *   +90° (North Pole)   → +42,957,189,152,768,000
 *   0° (Equator)        → 0
 *   -90° (South Pole)   → -42,957,189,152,768,000
 *   +51.477° (Heathrow) → +24,570,080,289,078,204 (approx)
 */
static inline int64_t EncodeLatitude(double deg) noexcept {
    return static_cast<int64_t>(deg * (10001750.0 / 90.0) * 65536.0 * 65536.0);
}

/**
 * @brief  Encode longitude into FSUIPC format.
 * @param  deg  Longitude in decimal degrees (positive = East).
 * @return Raw signed QWORD value (deg / 180 × INT64_MAX).
 *
 * DERIVATION:
 *   Formula maps [-180°, +180°] linearly to [INT64_MIN, INT64_MAX]:
 *     raw = deg / 180.0 × INT64_MAX
 *
 * EXAMPLES:
 *   +180° (Date Line East)  → +9,223,372,036,854,775,807 (INT64_MAX)
 *   0° (Prime Meridian)     → 0
 *   -180° (Date Line West)  → -9,223,372,036,854,775,808 (INT64_MIN)
 *   -0.461° (Heathrow)      → -23,619,005,360,000,000 (approx)
 */
static inline int64_t EncodeLongitude(double deg) noexcept {
    // Normalize to [-180, +180)
    while (deg >  180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return static_cast<int64_t>(deg / 180.0 * static_cast<double>(INT64_MAX));
}

/**
 * @brief  Encode altitude MSL into FSUIPC format.
 * @param  m  Altitude in metres above mean sea level.
 * @return Raw signed QWORD value (m × 65536).
 *
 * EXAMPLES:
 *   10,363 m (≈34,000 ft) → 679,149,568
 *   0 m (sea level)       → 0
 *   -100 m (below MSL)    → -6,553,600
 */
static inline int64_t EncodeAltitude(double m) noexcept {
    return static_cast<int64_t>(m * 65536.0);
}

#endif // FSUIPC_OFFSET_ENCODING_H
