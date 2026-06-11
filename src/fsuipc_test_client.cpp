/**
 * @file    fsuipc_test_client.cpp
 * @brief   A minimal FSUIPC-compatible IPC test client that connects to the
 *          server in fsuipc_server.cpp and reads all 10 simulated offsets.
 *
 * This file mirrors the behaviour of IPCuser.c from the FSUIPC SDK
 * (Pete Dowson / Adam Szofran) at the protocol level, but uses a
 * purpose-built request tracker instead of the pDest pointer trick so
 * that the code works correctly under both 32-bit and 64-bit builds.
 *
 * Usage
 * ─────
 *   Run fsuipc_server.exe first, then run this program.
 *   It polls all 10 offsets every second for 10 iterations and
 *   prints decoded, human-readable values to stdout.
 *
 * Build
 * ─────
 *   cl  /W4 /std:c++17 /O2 fsuipc_test_client.cpp /link user32.lib kernel32.lib
 *   g++ -std=c++17 -O2 -Wall fsuipc_test_client.cpp -o fsuipc_test_client -luser32 -lkernel32
 *
 * Platform: Windows (Win32 API)  — 32-bit or 64-bit build
 */

#include "fsuipc_ipc.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Client state
// ─────────────────────────────────────────────────────────────────────────────

static HWND   s_hWnd  = nullptr;  ///< UIPCMAIN server window handle
static UINT   s_Msg   = 0u;       ///< Registered "FsasmLib:IPC" message ID
static ATOM   s_Atom  = 0u;       ///< Global atom holding the mapping name
static HANDLE s_hMap  = nullptr;  ///< Named file-mapping handle
static BYTE*  s_pView = nullptr;  ///< View of the file-mapping
static BYTE*  s_pNext = nullptr;  ///< Next free byte in the mapping (write ptr)
static int    s_nTry  = 0;        ///< Monotonic counter for unique map names

// ─────────────────────────────────────────────────────────────────────────────
// Pending-read tracker
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Bookkeeping for one queued read request.
 *         After Process() returns we walk the file mapping and copy
 *         the server-filled bytes into the user's destination buffer,
 *         using viewOffset to locate the data without relying on pDest.
 */
struct PendingRead {
    uint32_t    offset;       ///< FSUIPC offset (for diagnostics)
    uint32_t    nBytes;       ///< Bytes requested
    void*       pDest;        ///< User's destination buffer
    ptrdiff_t   viewOffset;   ///< Byte offset inside s_pView where data lives
};

static std::vector<PendingRead> s_PendingReads;

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Connect to the FSUIPC server:
 *           1. Find the "UIPCMAIN" window.
 *           2. Register the "FsasmLib:IPC" window message.
 *           3. Create and map a named file-mapping buffer.
 * @return true on success.
 */
static bool ClientOpen() {
    // ── 1.  Find the server window ────────────────────────────────────────────
    s_hWnd = FindWindowExA(nullptr, nullptr, FSUIPC_WINDOW_CLASS, nullptr);
    if (!s_hWnd) {
        std::cerr << "[Client] Cannot find window \"" << FSUIPC_WINDOW_CLASS
                  << "\".  Is fsuipc_server.exe running?\n";
        return false;
    }
    std::cout << "[Client] Found \"" << FSUIPC_WINDOW_CLASS
              << "\" window (HWND=0x"
              << std::hex << reinterpret_cast<uintptr_t>(s_hWnd) << std::dec << ")\n";

    // ── 2.  Register the IPC window message ───────────────────────────────────
    s_Msg = RegisterWindowMessageA(FS6IPC_MSGNAME);
    if (s_Msg == 0u) {
        std::cerr << "[Client] RegisterWindowMessage failed: " << GetLastError() << "\n";
        return false;
    }
    std::cout << "[Client] IPC message ID: 0x" << std::hex << s_Msg << std::dec << "\n";

    // ── 3.  Build a unique name for the file mapping ──────────────────────────
    //   Format matches IPCuser.c: "FsasmLib:IPC:<ProcessID>:<nTry>"
    char szName[MAX_PATH];
    ++s_nTry;
    sprintf_s(szName, sizeof(szName),
              "%s:%08X:%d", FS6IPC_MSGNAME, GetCurrentProcessId(), s_nTry);

    // ── 4.  Create the named file mapping ─────────────────────────────────────
    s_Atom = GlobalAddAtomA(szName);
    if (s_Atom == 0u) {
        std::cerr << "[Client] GlobalAddAtom failed: " << GetLastError() << "\n";
        return false;
    }

    s_hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE,           // backed by paging file
        nullptr,                        // default security
        PAGE_READWRITE,
        0u, static_cast<DWORD>(IPC_BUFFER_MAX_SIZE + 256u),
        szName);

    if (!s_hMap || GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "[Client] CreateFileMapping failed: " << GetLastError() << "\n";
        GlobalDeleteAtom(s_Atom);
        s_Atom = 0u;
        return false;
    }

    s_pView = static_cast<BYTE*>(
        MapViewOfFile(s_hMap, FILE_MAP_ALL_ACCESS, 0u, 0u, 0u));
    if (!s_pView) {
        std::cerr << "[Client] MapViewOfFile failed: " << GetLastError() << "\n";
        CloseHandle(s_hMap); s_hMap = nullptr;
        GlobalDeleteAtom(s_Atom); s_Atom = 0u;
        return false;
    }

    s_pNext = s_pView;
    std::cout << "[Client] File mapping \"" << szName << "\" ready.\n\n";
    return true;
}

/**
 * @brief  Release all IPC resources.
 */
static void ClientClose() {
    if (s_Atom)  { GlobalDeleteAtom(s_Atom);     s_Atom  = 0u;      }
    if (s_pView) { UnmapViewOfFile(s_pView);      s_pView = nullptr; }
    if (s_hMap)  { CloseHandle(s_hMap);           s_hMap  = nullptr; }
    s_hWnd  = nullptr;
    s_pNext = nullptr;
    s_PendingReads.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Request queueing
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Queue a read request for @p nBytes bytes starting at FSUIPC
 *         @p offset.  Results will be copied into @p pDest after Process().
 */
static void QueueRead(uint32_t offset, uint32_t nBytes, void* pDest) {
    // Record the view offset of the data area (immediately after the header).
    ptrdiff_t dataViewOff =
        (s_pNext - s_pView) + static_cast<ptrdiff_t>(sizeof(IPC_ReadPacket));

    // Write the read-request header into the file mapping.
    auto* hdr     = reinterpret_cast<IPC_ReadPacket*>(s_pNext);
    hdr->dwId     = FS6IPC_READSTATEDATA_ID;
    hdr->dwOffset = offset;
    hdr->nBytes   = nBytes;
    hdr->pDest    = 0u;   // Not used for data copy – we track dataViewOff instead.

    // Zero the reception area so we get clean data if the server doesn't fill it.
    std::memset(s_pNext + sizeof(IPC_ReadPacket), 0, nBytes);

    s_pNext += sizeof(IPC_ReadPacket) + nBytes;

    // Book-keep the pending read.
    s_PendingReads.push_back({offset, nBytes, pDest, dataViewOff});
}

// Commented out - not used in this read-only test
// /**
//  * @brief  Queue a write request: copy @p nBytes from @p pSrc to FSUIPC
//  *         @p offset on the server side.
//  */
// static void QueueWrite(uint32_t offset, uint32_t nBytes, const void* pSrc) {
//     auto* hdr     = reinterpret_cast<IPC_WritePacket*>(s_pNext);
//     hdr->dwId     = FS6IPC_WRITESTATEDATA_ID;
//     hdr->dwOffset = offset;
//     hdr->nBytes   = nBytes;
//     std::memcpy(s_pNext + sizeof(IPC_WritePacket), pSrc, nBytes);
//     s_pNext += sizeof(IPC_WritePacket) + nBytes;
// }

// ─────────────────────────────────────────────────────────────────────────────
// Process: send all queued requests and retrieve results
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Flush all queued requests to the server, wait for the reply, then
 *         copy read results back into the caller's destination buffers.
 *
 * @return true if the server responded with FS6IPC_MESSAGE_SUCCESS.
 */
static bool Process() {
    if (!s_pView || s_pNext == s_pView) {
        std::cerr << "[Client] Process called with no pending requests.\n";
        return false;
    }

    // ── Write the chain terminator ────────────────────────────────────────────
    uint32_t terminator = 0u;
    std::memcpy(s_pNext, &terminator, sizeof(terminator));

    // ── Send message (synchronous, up to 2 s timeout) ────────────────────────
    DWORD_PTR dwResult = 0u;
    int       retries  = 0;
    while (retries < 5 &&
           !SendMessageTimeoutA(
               s_hWnd,
               s_Msg,
               static_cast<WPARAM>(s_Atom),   // wParam = atom of mapping name
               0,                              // lParam = offset into mapping (0)
               SMTO_BLOCK,
               2000u,
               &dwResult))
    {
        Sleep(100);
        ++retries;
    }

    if (retries >= 5) {
        std::cerr << "[Client] SendMessageTimeout timed out after " << retries << " retries.\n";
        s_pNext = s_pView;
        s_PendingReads.clear();
        return false;
    }

    if (dwResult != FS6IPC_MESSAGE_SUCCESS) {
        std::cerr << "[Client] Server returned failure (" << dwResult << ").\n";
        s_pNext = s_pView;
        s_PendingReads.clear();
        return false;
    }

    // ── Copy returned data back into user buffers ─────────────────────────────
    //   The server has filled the data bytes in-place inside the file mapping.
    //   We find them using the viewOffset we recorded in QueueRead().
    for (const auto& pr : s_PendingReads) {
        if (pr.pDest && pr.nBytes) {
            std::memcpy(pr.pDest,
                        s_pView + pr.viewOffset,
                        pr.nBytes);
        }
    }

    // ── Reset write pointer and pending-read list ────────────────────────────
    s_pNext = s_pView;
    s_PendingReads.clear();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decoding helpers (raw FSUIPC units → human-readable values)
// ─────────────────────────────────────────────────────────────────────────────

/** DWORD heading raw → degrees (0–359.9…) */
static double DecHeading(uint32_t raw)  { return raw * 360.0 / 65536.0; }

/** DWORD ground-speed raw → knots */
static double DecGS_kts(uint32_t raw)  { return (raw / 65536.0) / 0.514444; }

/** DWORD IAS raw → knots */
static double DecIAS_kts(uint32_t raw) { return raw / 128.0; }

/** DWORD signed VS raw → feet per minute */
static double DecVS_fpm(int32_t raw)   { return (raw / 256.0) / 0.00508; }

/** WORD barometric raw → millibars */
static double DecBaro_mb(uint16_t raw) { return raw / 16.0; }

/** QWORD signed lat raw → decimal degrees */
static double DecLat(int64_t raw) {
    return raw * 90.0 / (10001750.0 * 65536.0 * 65536.0);
}

/** QWORD signed lon raw → decimal degrees */
static double DecLon(int64_t raw) {
    return raw * 180.0 / static_cast<double>(INT64_MAX);
}

/** QWORD altitude raw → feet */
static double DecAlt_ft(int64_t raw) {
    return (raw / 65536.0) * 3.28084;
}

/** DWORD signed pitch raw → degrees (positive = nose up) */
static double DecPitch(int32_t raw) {
    return raw * 360.0 / (65536.0 * 65536.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout <<
        "\n"
        "+===========================================+\n"
        "|      FSUIPC IPC  Test Client            |\n"
        "+===========================================+\n"
        "\n";

    if (!ClientOpen()) {
        return 1;
    }

    // ── First: read the two internal version offsets so we can validate ───────
    uint32_t fsuipcVer = 0u;
    uint32_t fsVer     = 0u;
    QueueRead(FSUIPC::VERSION,    4, &fsuipcVer);
    QueueRead(FSUIPC::FS_VERSION, 4, &fsVer);
    if (!Process()) {
        std::cerr << "[Client] Failed to read version offsets.\n";
        ClientClose();
        return 1;
    }

    // Validate: high word of fsVer must be 0xFADE, and fsuipcVer >= 0x19980005.
    const uint32_t fadeMask = (fsVer & 0xFFFF0000u);
    if (fadeMask != 0xFADE0000u) {
        std::cerr << "[Client] FS version validity token is wrong (0x"
                  << std::hex << fsVer << std::dec
                  << "). Server may not be a valid FSUIPC server.\n";
        ClientClose();
        return 1;
    }
    if (fsuipcVer < 0x19980005u) {
        std::cerr << "[Client] FSUIPC version too old (0x" << std::hex
                  << fsuipcVer << std::dec << ").\n";
        ClientClose();
        return 1;
    }

    std::cout << "[Client] Validated:  FSUIPC=0x" << std::hex << fsuipcVer
              << "  FS_type=0x" << (fsVer & 0xFFFFu) << std::dec << "\n\n";

    // ── Polling loop: read all 10 offsets, print decoded values ───────────────
    constexpr int POLL_COUNT    = 10;
    constexpr int POLL_DELAY_MS = 1000;

    for (int poll = 1; poll <= POLL_COUNT; ++poll) {
        // Storage for each offset.
        uint32_t  hdg_raw   = 0;
        uint32_t  gs_raw    = 0;
        uint32_t  ias_raw   = 0;
        int32_t   vs_raw    = 0;
        uint16_t  baro_raw  = 0;
        uint16_t  onGnd_raw = 0;
        int64_t   lat_raw   = 0;
        int64_t   lon_raw   = 0;
        int64_t   alt_raw   = 0;
        int32_t   pitch_raw = 0;

        // Queue all 10 reads in one batch.
        QueueRead(FSUIPC::MAG_HEADING,  sizeof(hdg_raw),   &hdg_raw);
        QueueRead(FSUIPC::GROUND_SPEED, sizeof(gs_raw),    &gs_raw);
        QueueRead(FSUIPC::IAS,          sizeof(ias_raw),   &ias_raw);
        QueueRead(FSUIPC::VERT_SPEED,   sizeof(vs_raw),    &vs_raw);
        QueueRead(FSUIPC::ALTIMETER,    sizeof(baro_raw),  &baro_raw);
        QueueRead(FSUIPC::ON_GROUND,    sizeof(onGnd_raw), &onGnd_raw);
        QueueRead(FSUIPC::LATITUDE,     sizeof(lat_raw),   &lat_raw);
        QueueRead(FSUIPC::LONGITUDE,    sizeof(lon_raw),   &lon_raw);
        QueueRead(FSUIPC::ALTITUDE_MSL, sizeof(alt_raw),   &alt_raw);
        QueueRead(FSUIPC::PITCH,        sizeof(pitch_raw), &pitch_raw);

        if (!Process()) {
            std::cerr << "[Client] Process() failed on poll " << poll << "\n";
            break;
        }

        // Print decoded values.
        std::cout << std::fixed << std::setprecision(1);
        std::cout
            << "--- Poll " << std::setw(2) << poll << " / " << POLL_COUNT
            << " -------------------------------------------\n"
            << "  Heading        : " << std::setw(7) << DecHeading(hdg_raw)  << " deg\n"
            << "  Ground Speed   : " << std::setw(7) << DecGS_kts(gs_raw)    << " kts\n"
            << "  IAS            : " << std::setw(7) << DecIAS_kts(ias_raw)  << " kts\n"
            << "  Vertical Speed : " << std::setw(7) << DecVS_fpm(vs_raw)    << " fpm\n"
            << "  Altimeter      : " << std::setw(7) << DecBaro_mb(baro_raw) << " mb\n"
            << "  On Ground      : " << (onGnd_raw ? "YES (on ground)" : "NO  (airborne)") << "\n"
            << std::setprecision(4)
            << "  Latitude       : " << std::setw(10) << DecLat(lat_raw)     << " deg\n"
            << "  Longitude      : " << std::setw(10) << DecLon(lon_raw)     << " deg\n"
            << std::setprecision(0)
            << "  Altitude MSL   : " << std::setw(7) << DecAlt_ft(alt_raw)   << " ft\n"
            << std::setprecision(2)
            << "  Pitch          : " << std::setw(7) << DecPitch(pitch_raw)  << " deg\n"
            << "\n";

        if (poll < POLL_COUNT) Sleep(POLL_DELAY_MS);
    }

    ClientClose();
    std::cout << "[Client] Done.\n";
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    return 0;
}
