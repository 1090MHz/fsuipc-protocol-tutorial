/**
 * @file    fsuipc_ipc.h
 * @brief   Shared FSUIPC FS6IPC protocol definitions used by both the server
 *          and the test client.
 *
 * Protocol overview
 * -----------------
 * This is the "FsasmLib:IPC" protocol originally designed by Adam Szofran
 * for FS6 and extended by Pete Dowson for FSUIPC.
 *
 * IPC handshake (client side, mirrors IPCuser.c from the FSUIPC SDK):
 *
 *   1. FindWindowEx(NULL, NULL, "UIPCMAIN", NULL)  → gets server HWND
 *   2. RegisterWindowMessage("FsasmLib:IPC")        → gets shared message ID
 *   3. CreateFileMapping(INVALID_HANDLE_VALUE, …, PAGE_READWRITE, …, szName)
 *      where szName = "FsasmLib:IPC:<ProcessID>:<nTry>"
 *   4. GlobalAddAtom(szName)                        → wParam for the message
 *   5. Write request packets starting at pView[0]:
 *        [ReadPkt] [ReadPkt] … [WritePkt] … [DWORD 0 terminator]
 *   6. SendMessageTimeout(hWnd, ipcMsg, atom, 0, SMTO_BLOCK, 2000, &retval)
 *   7. Walk pView again; for each ReadPkt copy data bytes to pDest
 *
 * Packet layout (both read and write share the "FsasmLib" convention):
 *
 *   READ  (FS6IPC_READSTATEDATA_ID  = 1):
 *     ┌─ DWORD  dwId     (= 1)
 *     ├─ DWORD  dwOffset (FSUIPC byte offset)
 *     ├─ DWORD  nBytes   (bytes to read)
 *     ├─ DWORD  pDest    (client buffer pointer, 32-bit truncated)
 *     └─ BYTE   data[nBytes]  ← server fills these in
 *
 *   WRITE (FS6IPC_WRITESTATEDATA_ID = 2):
 *     ┌─ DWORD  dwId     (= 2)
 *     ├─ DWORD  dwOffset (FSUIPC byte offset)
 *     ├─ DWORD  nBytes   (bytes to write)
 *     └─ BYTE   data[nBytes]  ← client provides these
 *
 *   TERMINATOR: first DWORD == 0
 *
 * Architecture note
 * -----------------
 * This header fixes pDest as uint32_t and uses #pragma pack(1) so that
 * sizeof(ReadPkt) == 16 and sizeof(WritePkt) == 12 regardless of whether
 * the code is compiled 32-bit or 64-bit. Both the server and the test
 * client in this project use this same fixed layout.
 *
 * The official FSUIPC SDK (IPCuser.c) uses `void* pDest`, so its struct
 * sizes differ on 64-bit (24 bytes with default alignment). Use the
 * UIPC64_SDK_C_version2 headers when linking against unmodified SDK code.
 */

#pragma once
#ifndef FSUIPC_IPC_H
#define FSUIPC_IPC_H

#include <windows.h>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Protocol identifiers
// ─────────────────────────────────────────────────────────────────────────────

/// Window class (and title) of the FSUIPC server window.
/// Clients call: FindWindowEx(NULL, NULL, FSUIPC_WINDOW_CLASS, NULL)
#define FSUIPC_WINDOW_CLASS        "UIPCMAIN"

/// Name passed to RegisterWindowMessage().  The returned UINT is the message
/// ID used in SendMessageTimeout() / WM_handler.
#define FS6IPC_MSGNAME             "FsasmLib:IPC"

/// Return value placed in dwResult by a successful IPC SendMessage call.
#define FS6IPC_MESSAGE_SUCCESS     1u
#define FS6IPC_MESSAGE_FAILURE     0u

/// Packet type IDs stored in dwId.
#define FS6IPC_READSTATEDATA_ID    1u
#define FS6IPC_WRITESTATEDATA_ID   2u

/// Maximum safe size for a single IPC file-mapping buffer (kept below 32 KiB
/// to avoid any potential 16-bit sign issues, as noted in IPCuser.c).
#define IPC_BUFFER_MAX_SIZE        0x7F00u

// ─────────────────────────────────────────────────────────────────────────────
// Packet structures  (always 16 / 12 bytes — see architecture note above)
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)

/**
 * @brief Header of a READ request packet written by the client.
 *        The server fills in data[0..nBytes-1] and leaves everything else.
 */
struct IPC_ReadPacket {
    uint32_t dwId;      ///< FS6IPC_READSTATEDATA_ID  (= 1)
    uint32_t dwOffset;  ///< FSUIPC byte offset to read
    uint32_t nBytes;    ///< Number of bytes to read
    uint32_t pDest;     ///< Client destination pointer (stored as 32-bit;
                        ///< server does not dereference this field)
    // Immediately followed by nBytes bytes.
    // Client zeroes them before sending; server fills them on return.
};

/**
 * @brief Header of a WRITE request packet written by the client.
 *        The data to write follows immediately after the header.
 */
struct IPC_WritePacket {
    uint32_t dwId;      ///< FS6IPC_WRITESTATEDATA_ID (= 2)
    uint32_t dwOffset;  ///< FSUIPC byte offset to write
    uint32_t nBytes;    ///< Number of bytes to write
    // Immediately followed by nBytes bytes of data.
};

#pragma pack(pop)

static_assert(sizeof(IPC_ReadPacket)  == 16, "IPC_ReadPacket must be 16 bytes");
static_assert(sizeof(IPC_WritePacket) == 12, "IPC_WritePacket must be 12 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// FSUIPC version / FS-type identifiers
// (stored at the two internal offsets 0x3304 and 0x3308)
// ─────────────────────────────────────────────────────────────────────────────

/// Stored at offset 0x3304.  The version is stored in BCD-like format:
///   HIWORD = version digits as hex (e.g. 7.0.0.0 → 0x7000)
///   LOWORD = build letter ordinal  (a=1, b=2, …)
/// Must be >= 0x19980005 (= version 1.998 build 'e') for clients to accept it.
/// FSUIPC 7.0.0 build 'a' → HIWORD = 0x7000, LOWORD = 0x0001 → 0x70000001
#define FSUIPC_SIM_VERSION          0x70000001u   // FSUIPC 7.0.0 build 'a'

/// Stored at offset 0x3308.  The high-word MUST be 0xFADE (validity token).
/// Low word identifies the simulator type.  Values (from FSUIPC_User.h):
///   1 = FS98, 2 = FS2000, 3 = CFS2, 4 = CFS1, 5 = Fly!, 6 = FS2002,
///   7 = FS2004, 8 = FSX, 9 = ESP, 10 = Prepar3D
#define FSUIPC_FS_TYPE_VALUE        0xFADE0009u   // FSX (9)

// ─────────────────────────────────────────────────────────────────────────────
// The 10 simulated flight-data offsets
// All sizes and unit encodings are taken directly from the FSUIPC SDK
// "Offsets Status" document by Pete Dowson.
// ─────────────────────────────────────────────────────────────────────────────

namespace FSUIPC {

    // ── Internal / connection offsets ────────────────────────────────────────
    constexpr uint32_t VERSION        = 0x3304; ///< 4 B — FSUIPC version
    constexpr uint32_t FS_VERSION     = 0x3308; ///< 4 B — FS type + validity

    // ── 10 simulated flight-data offsets ─────────────────────────────────────

    /// 4 B (DWORD) — Magnetic compass heading.
    /// Encoding: raw = heading_deg × 65536 / 360  (full circle = 65536)
    /// Decode:   heading_deg = raw × 360.0 / 65536.0
    constexpr uint32_t MAG_HEADING    = 0x0238;

    /// 4 B (DWORD) — Ground speed in metres per second × 65536.
    /// Decode: gs_mps = raw / 65536.0;  gs_kts = gs_mps / 0.514444
    constexpr uint32_t GROUND_SPEED   = 0x02B4;

    /// 4 B (DWORD) — Indicated airspeed in knots × 128.
    /// Decode: ias_kts = raw / 128.0
    constexpr uint32_t IAS            = 0x02BC;

    /// 4 B (DWORD, signed) — Vertical speed in metres per second × 256.
    /// Decode: vs_mps = raw / 256.0;  vs_fpm = vs_mps / 0.00508
    constexpr uint32_t VERT_SPEED     = 0x02C8;

    /// 2 B (WORD) — Barometric (Kollsman) pressure setting in millibars × 16.
    /// Decode: baro_mb = raw / 16.0
    constexpr uint32_t ALTIMETER      = 0x0330;

    /// 2 B (WORD) — On-ground flag.  0 = airborne, 1 = on ground.
    constexpr uint32_t ON_GROUND      = 0x0366;

    /// 8 B (QWORD, signed) — Aircraft latitude.
    /// Decode: lat_deg = raw × 90.0 / (10001750.0 × 65536.0 × 65536.0)
    /// Encode: raw = lat_deg × 10001750.0 / 90.0 × 65536.0 × 65536.0
    constexpr uint32_t LATITUDE       = 0x0560;

    /// 8 B (QWORD, signed) — Aircraft longitude.
    /// Decode: lon_deg = raw × 180.0 / static_cast<double>(INT64_MAX)
    /// Encode: raw = lon_deg / 180.0 × INT64_MAX
    constexpr uint32_t LONGITUDE      = 0x0568;

    /// 8 B (QWORD, signed) — Altitude above MSL in metres × 65536.
    /// Decode: alt_m = raw / 65536.0;  alt_ft = alt_m × 3.28084
    constexpr uint32_t ALTITUDE_MSL   = 0x0570;

    /// 4 B (DWORD, signed) — Pitch angle.
    /// Decode: pitch_deg = raw × 360.0 / (65536.0 × 65536.0)
    /// Positive = nose up, negative = nose down.
    constexpr uint32_t PITCH          = 0x0578;

} // namespace FSUIPC

#endif // FSUIPC_IPC_H
