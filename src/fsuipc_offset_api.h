/**
 * @file    fsuipc_offset_api.h
 * @brief   FSUIPC offset encoder API (public interface)
 *
 * This header defines the public API for accessing FSUIPC offset encoders.
 * The architecture is split into three modular layers:
 *
 *   fsuipc_offset_encoding.h → Encoding infrastructure (testable)
 *   fsuipc_offset_table.h    → Table data ONLY (can be code-generated)
 *   fsuipc_offset_api.h      → Public API (this file - stable interface)
 *
 * Benefits of this modular separation:
 *   ✅ Server and client can both use the same API
 *   ✅ Easy to maintain (single source of truth)
 *   ✅ Scalable: table can be code-generated without touching API
 *   ✅ Testable: encoding functions can be unit-tested independently
 *   ✅ Stable: API never changes when adding offsets
 *   ✅ Zero-overhead design (raw function pointers, not std::function)
 *
 * PERFORMANCE DESIGN:
 * ───────────────────
 * This implementation uses raw function pointers instead of std::function for:
 *   • Memory: 24 bytes/entry vs 32-40 bytes (std::function typical size)
 *   • Speed: Direct call (~2ns) vs virtual dispatch (~20-50ns) (10x faster)
 *   • Scalability: At 1000 offsets × 60Hz = 60,000 calls/sec, overhead matters!
 *
 * USAGE (Server):
 *   size_t count;
 *   const OffsetEncoder* table = GetOffsetTable(count);
 *   for (size_t i = 0; i < count; ++i) {
 *       table[i].encode(simState, buffer);  // buffer = base of 64KB FSUIPC memory
 *   }
 *
 * USAGE (Client - validation):
 *   // Check if an offset is supported
 *   if (IsOffsetSupported(0x0238)) { ... }
 *
 */

#pragma once
#ifndef FSUIPC_OFFSET_API_H
#define FSUIPC_OFFSET_API_H

#include "fsuipc_offset_encoding.h"  // Provides SimState, WriteOffset<T>, and all encoding functions

// ─────────────────────────────────────────────────────────────────────────────
// Offset encoder table structure
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Offset encoder entry: maps one FSUIPC offset to its encoding function.
 *
 * Each entry describes:
 *   - offset:      FSUIPC byte offset address
 *   - size:        Size in bytes (1, 2, 4, or 8)
 *   - encode:      Function pointer that reads SimState and writes to buffer
 *   - description: Human-readable name for documentation/logging
 *
 * PERFORMANCE: Raw function pointers (not std::function) for zero overhead:
 *   - Memory: 24 bytes per entry vs 32-40 bytes for std::function
 *   - Call overhead: Direct call (~2ns) vs virtual dispatch (~20-50ns)
 *   - At 60Hz × 1000 offsets = 60,000 calls/sec, this matters!
 *
 * For high-frequency updates (60+ Hz), also use CACHED_REF pattern (see X-Plane integration).
 */
struct OffsetEncoder {
    uint32_t offset;                                  ///< FSUIPC offset address
    uint32_t size;                                    ///< Size in bytes
    void (*encode)(const SimState&, uint8_t*);       ///< Encoding function (raw ptr, zero overhead)
    const char* description;                          ///< Human-readable name
};

// ─────────────────────────────────────────────────────────────────────────────
// Include table data (isolated for code generation)
// ─────────────────────────────────────────────────────────────────────────────

#include "fsuipc_offset_table.h"  // Defines g_OffsetEncoderTable[]

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Get the offset encoder table.
 * @return Pointer to the first entry and count via out parameter.
 */
inline const OffsetEncoder* GetOffsetTable(size_t& out_count) noexcept {
    out_count = sizeof(g_OffsetEncoderTable) / sizeof(g_OffsetEncoderTable[0]);
    return g_OffsetEncoderTable;
}

/**
 * @brief  Get the offset encoder table (C++ range-based for loop friendly).
 */
inline const OffsetEncoder* begin(const OffsetEncoder*) noexcept {
    return g_OffsetEncoderTable;
}

inline const OffsetEncoder* end(const OffsetEncoder*) noexcept {
    size_t count;
    GetOffsetTable(count);
    return g_OffsetEncoderTable + count;
}

/**
 * @brief  Check if an offset is supported by this server.
 * @param  offset  FSUIPC offset address to check.
 * @return true if the offset is in the table, false otherwise.
 *
 * OPTIMIZATION: For large tables (100+ offsets), use binary search or hash map.
 */
inline bool IsOffsetSupported(uint32_t offset) noexcept {
    size_t count;
    const OffsetEncoder* table = GetOffsetTable(count);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].offset == offset) return true;
    }
    return false;
}

/**
 * @brief  Encode all offsets from SimState into the offset buffer.
 * @param  state  Source simulation state.
 * @param  buffer Destination buffer (must be at least 64KB).
 *
 * This is the main entry point called by the simulator update loop.
 * Thread safety: Caller must ensure exclusive access to buffer.
 */
inline void EncodeAllOffsets(const SimState& state, uint8_t* buffer) noexcept {
    size_t count;
    const OffsetEncoder* table = GetOffsetTable(count);
    for (size_t i = 0; i < count; ++i) {
        table[i].encode(state, buffer);
    }
}

#endif // FSUIPC_OFFSET_API_H
