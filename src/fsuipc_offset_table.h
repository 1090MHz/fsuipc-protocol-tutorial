/**
 * @file    fsuipc_offset_table.h
 * @brief   FSUIPC offset encoding table data (generated or hand-maintained)
 *
 * This file contains ONLY the table data - no API, no struct definitions.
 * By isolating the table data:
 *
 *   ✅ Code generation can overwrite this file without touching API
 *   ✅ Easy to maintain multiple table variants (dev, prod, test)
 *   ✅ Clean separation: data vs interface
 *   ✅ Table can be regenerated from spec files without risk
 *
 * MODULAR ARCHITECTURE:
 * ─────────────────────
 *   fsuipc_offset_encoding.h → Low-level encoding functions
 *   fsuipc_offset_api.h      → OffsetEncoder struct + Public API
 *   fsuipc_offset_table.h    → Table data ONLY (this file)
 *
 * CODE GENERATION WORKFLOW:
 * ─────────────────────────
 *   1. Maintain authoritative offset spec: offsets.json
 *   2. Run generator: gen_offsets.py --input offsets.json --output fsuipc_offset_table.h
 *   3. Generator overwrites THIS FILE ONLY (never touches API!)
 *   4. Rebuild project
 *
 * DO NOT INCLUDE THIS FILE DIRECTLY!
 * ───────────────────────────────────
 * Always include fsuipc_offset_api.h instead, which provides the full API.
 * This file is an implementation detail included by fsuipc_offset_api.h.
 *
 * HOW TO ADD A NEW OFFSET (MANUAL):
 * ──────────────────────────────────
 *   1. Add encoding helper function to fsuipc_offset_encoding.h (if needed)
 *   2. Add one entry to g_OffsetEncoderTable[] below
 *   3. Keep sorted by offset address
 *   4. Rebuild
 *
 * LAMBDA-TO-FUNCTION-POINTER CONVERSION:
 * ───────────────────────────────────────
 * C++ allows capture-less lambdas to automatically convert to function pointers:
 *
 *   void (*fn)(const SimState&, uint8_t*) = [](const SimState& s, uint8_t* buf) {
 *       WriteOffset<uint32_t>(buf, 0x0238, EncodeHeading(s.heading_deg));
 *   };  // ← No captures = can convert to function pointer (zero overhead!)
 *
 * Each lambda below has no captures (no [&] or [=]), so the compiler generates
 * a static function and stores just an 8-byte pointer.
 */

#pragma once
#ifndef FSUIPC_OFFSET_TABLE_H
#define FSUIPC_OFFSET_TABLE_H

// This file requires OffsetEncoder struct and encoding functions from parent includes.
// Do not include this file directly - include fsuipc_offset_api.h instead!

// ─────────────────────────────────────────────────────────────────────────────
// OFFSET ENCODING TABLE DATA
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Master table of all supported FSUIPC offsets.
 *
 * ORDERING: Sorted by offset address for easier maintenance and binary search.
 *
 * TABLE SIZE: Currently 12 entries (2 internal version + 10 flight data).
 *
 * SCALABILITY: For 100+ offsets:
 *   - Generate this file from offsets.json spec
 *   - Add binary search lookup in GetOffsetByAddress()
 *   - Add dirty tracking to skip unchanged offsets
 *   - Consider hash map for O(1) offset lookup
 */
static const OffsetEncoder g_OffsetEncoderTable[] = {

    // ── Internal version offsets (required by FSUIPC_Open to validate) ────────
    { FSUIPC::VERSION, 4,
      [](const SimState&, uint8_t* buf) {
          WriteOffset<uint32_t>(buf, FSUIPC::VERSION, FSUIPC_SIM_VERSION);
      },
      "FSUIPC Version (0x70000001 = v7.0.0 build a)" },

    { FSUIPC::FS_VERSION, 4,
      [](const SimState&, uint8_t* buf) {
          WriteOffset<uint32_t>(buf, FSUIPC::FS_VERSION, FSUIPC_FS_TYPE_VALUE);
      },
      "FS Version / Validity (0xFADE0009 = FSX)" },

    // ── Flight data offsets (sorted by address) ───────────────────────────────

    { FSUIPC::MAG_HEADING, 4,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<uint32_t>(buf, FSUIPC::MAG_HEADING, EncodeHeading(s.heading_deg));
      },
      "Magnetic Heading (deg × 65536/360)" },

    { FSUIPC::GROUND_SPEED, 4,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<uint32_t>(buf, FSUIPC::GROUND_SPEED, EncodeGroundSpeed(s.gs_mps));
      },
      "Ground Speed (m/s × 65536)" },

    { FSUIPC::IAS, 4,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<uint32_t>(buf, FSUIPC::IAS, EncodeIAS(s.ias_kts));
      },
      "Indicated Airspeed (kts × 128)" },

    { FSUIPC::VERT_SPEED, 4,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<int32_t>(buf, FSUIPC::VERT_SPEED, EncodeVerticalSpeed(s.vs_mps));
      },
      "Vertical Speed (m/s × 256, signed)" },

    { FSUIPC::ALTIMETER, 2,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<uint16_t>(buf, FSUIPC::ALTIMETER, EncodeBarometer(s.baro_mb));
      },
      "Altimeter Pressure (mb × 16)" },

    { FSUIPC::ON_GROUND, 2,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<uint16_t>(buf, FSUIPC::ON_GROUND, s.on_ground ? 1u : 0u);
      },
      "On Ground (0=airborne, 1=ground)" },

    { FSUIPC::LATITUDE, 8,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<int64_t>(buf, FSUIPC::LATITUDE, EncodeLatitude(s.lat_deg));
      },
      "Latitude (deg × 10001750/90 × 65536², signed)" },

    { FSUIPC::LONGITUDE, 8,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<int64_t>(buf, FSUIPC::LONGITUDE, EncodeLongitude(s.lon_deg));
      },
      "Longitude (deg/180 × INT64_MAX, signed)" },

    { FSUIPC::ALTITUDE_MSL, 8,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<int64_t>(buf, FSUIPC::ALTITUDE_MSL, EncodeAltitude(s.alt_m));
      },
      "Altitude MSL (m × 65536, signed)" },

    { FSUIPC::PITCH, 4,
      [](const SimState& s, uint8_t* buf) {
          WriteOffset<int32_t>(buf, FSUIPC::PITCH, EncodePitch(s.pitch_deg));
      },
      "Pitch Angle (deg × 65536²/360, signed)" },
};

#endif // FSUIPC_OFFSET_TABLE_H
