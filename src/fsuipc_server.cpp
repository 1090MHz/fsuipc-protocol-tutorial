/**
 * @file    fsuipc_server.cpp
 * @brief   A standalone FSUIPC-compatible IPC server that simulates 10 of
 *          the most commonly used FSUIPC offsets from the FSUIPC SDK.
 *
 * How it works
 * ─────────────
 *  1.  A hidden top-level Win32 window is created with class "UIPCMAIN".
 *      Clients discover the server with:
 *        FindWindowEx(NULL, NULL, "UIPCMAIN", NULL)
 *
 *  2.  The FS6IPC window message ("FsasmLib:IPC") is registered.
 *      Clients send this message to trigger IPC processing.
 *
 *  3.  A background thread updates the 64 KB offset-memory block every
 *      500 ms with fresh simulated flight data (climbing cruise flight
 *      over London Heathrow area).
 *
 *  4.  When a client message arrives (wParam = atom of named file mapping,
 *      lParam = byte offset into that mapping):
 *        a. Open the named file mapping via GlobalGetAtomName + OpenFileMapping
 *        b. Walk the packet chain from lParam offset
 *        c. READ  packets → copy from offset memory into the client mapping
 *        d. WRITE packets → copy from client mapping into offset memory
 *        e. Unmap & close; return FS6IPC_MESSAGE_SUCCESS (1)
 *
 * Served offsets
 * ──────────────
 *   Internal (required for client library to validate connection):
 *     0x3304  FSUIPC version
 *     0x3308  FS-type + 0xFADE validity token
 *
 *   Flight data (the 10 simulated offsets):
 *     0x0238  Magnetic compass heading     (4 B)
 *     0x02B4  Ground speed                 (4 B)
 *     0x02BC  Indicated airspeed           (4 B)
 *     0x02C8  Vertical speed               (4 B, signed)
 *     0x0330  Altimeter pressure setting   (2 B)
 *     0x0366  On-ground flag               (2 B)
 *     0x0560  Aircraft latitude            (8 B, signed)
 *     0x0568  Aircraft longitude           (8 B, signed)
 *     0x0570  Altitude MSL                 (8 B, signed)
 *     0x0578  Pitch angle                  (4 B, signed)
 *
 * Build
 * ─────
 *   cl  /W4 /std:c++17 /O2 fsuipc_server.cpp  /link user32.lib kernel32.lib
 *   g++ -std=c++17 -O2 -Wall fsuipc_server.cpp -o fsuipc_server -luser32 -lkernel32
 *
 * Platform: Windows (Win32 API)  — 32-bit or 64-bit build
 *
 * References
 * ──────────
 *   ▪ FSUIPC SDK by Pete Dowson  (IPCuser.c / FSUIPC_User.h / IPCuser.h)
 *   ▪ FS6IPC original by Adam Szofran
 *   ▪ FSUIPC Offsets Status document (Pete Dowson)
 */

#include "fsuipc_ipc.h"

#define _USE_MATH_DEFINES
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Internal offset memory
// ─────────────────────────────────────────────────────────────────────────────

/// Total addressable FSUIPC offset space (64 KiB).
/// In real FSUIPC this is much larger, but 64 KiB covers all common offsets.
static constexpr size_t OFFSET_MEM_SIZE = 0x10000u;

alignas(8) static uint8_t  g_OffsetMem[OFFSET_MEM_SIZE];
static std::mutex           g_OffsetMutex;

// ─────────────────────────────────────────────────────────────────────────────
// Offset memory accessors
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Write a typed value into the offset memory at a given FSUIPC offset.
 * @note   Caller must hold g_OffsetMutex.
 */
template<typename T>
static inline void WriteOff(uint32_t off, const T& val) noexcept {
    if (off + sizeof(T) <= OFFSET_MEM_SIZE) {
        std::memcpy(&g_OffsetMem[off], &val, sizeof(T));
    }
}

/**
 * @brief  Read a typed value from the offset memory.
 * @note   Caller must hold g_OffsetMutex.
 */
template<typename T>
static inline T ReadOff(uint32_t off) noexcept {
    T v{};
    if (off + sizeof(T) <= OFFSET_MEM_SIZE) {
        std::memcpy(&v, &g_OffsetMem[off], sizeof(T));
    }
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoding helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Heading / bank / pitch raw encoding helper: degrees → raw DWORD. */
static inline uint32_t EncHeading(double deg) noexcept {
    return static_cast<uint32_t>(fmod(deg, 360.0) * 65536.0 / 360.0);
}

/** Signed pitch: degrees → raw (positive = nose up). */
static inline int32_t EncPitch(double deg) noexcept {
    return static_cast<int32_t>(deg * 65536.0 * 65536.0 / 360.0);
}

/** Ground speed: m/s → raw DWORD (m/s × 65536). */
static inline uint32_t EncGS(double mps) noexcept {
    return static_cast<uint32_t>(mps * 65536.0);
}

/** IAS: knots → raw DWORD (knots × 128). */
static inline uint32_t EncIAS(double kts) noexcept {
    return static_cast<uint32_t>(kts * 128.0);
}

/** Vertical speed: m/s → raw int32 (m/s × 256, signed). */
static inline int32_t EncVS(double mps) noexcept {
    return static_cast<int32_t>(mps * 256.0);
}

/** Barometric pressure: mb → raw WORD (mb × 16). */
static inline uint16_t EncBaro(double mb) noexcept {
    return static_cast<uint16_t>(mb * 16.0);
}

/**
 * @brief  Latitude: decimal degrees → signed QWORD.
 * @details
 *   From the FSUIPC SDK "Offsets Status" document:
 *     raw = lat_deg × (10001750.0 / 90.0) × 65536.0 × 65536.0
 */
static inline int64_t EncLat(double deg) noexcept {
    return static_cast<int64_t>(deg * (10001750.0 / 90.0) * 65536.0 * 65536.0);
}

/**
 * @brief  Longitude: decimal degrees → signed QWORD.
 * @details
 *   The longitude fills the full INT64 range for ±180°.
 *   Encode: raw = deg / 180.0 × INT64_MAX
 */
static inline int64_t EncLon(double deg) noexcept {
    // Normalise to [-180, +180)
    while (deg >  180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return static_cast<int64_t>(deg / 180.0 * static_cast<double>(INT64_MAX));
}

/**
 * @brief  Altitude MSL: metres → signed QWORD (metres × 65536).
 */
static inline int64_t EncAlt(double m) noexcept {
    return static_cast<int64_t>(m * 65536.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulation state & updater thread
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  All mutable flight-data parameters for the simulated aircraft.
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

/**
 * @brief  Write the current SimState into g_OffsetMem.
 * @note   Caller must hold g_OffsetMutex.
 */
static void FlushSimState(const SimState& s) noexcept {
    // ── Internal version offsets (required by FSUIPC_Open to validate) ───────
    WriteOff<uint32_t>(FSUIPC::VERSION,    FSUIPC_SIM_VERSION);   // 0x70000001 = v7.0.0 build a
    WriteOff<uint32_t>(FSUIPC::FS_VERSION, FSUIPC_FS_TYPE_VALUE);

    // ── 10 flight-data offsets ────────────────────────────────────────────────
    WriteOff<uint32_t>(FSUIPC::MAG_HEADING,  EncHeading(s.heading_deg));
    WriteOff<uint32_t>(FSUIPC::GROUND_SPEED, EncGS(s.gs_mps));
    WriteOff<uint32_t>(FSUIPC::IAS,          EncIAS(s.ias_kts));
    WriteOff<int32_t> (FSUIPC::VERT_SPEED,   EncVS(s.vs_mps));
    WriteOff<uint16_t>(FSUIPC::ALTIMETER,    EncBaro(s.baro_mb));
    WriteOff<uint16_t>(FSUIPC::ON_GROUND,    s.on_ground ? 1u : 0u);
    WriteOff<int64_t> (FSUIPC::LATITUDE,     EncLat(s.lat_deg));
    WriteOff<int64_t> (FSUIPC::LONGITUDE,    EncLon(s.lon_deg));
    WriteOff<int64_t> (FSUIPC::ALTITUDE_MSL, EncAlt(s.alt_m));
    WriteOff<int32_t> (FSUIPC::PITCH,        EncPitch(s.pitch_deg));
}

static std::atomic<bool> g_Running{true};

/**
 * @brief  Background thread: updates the offset memory every 500 ms with
 *         smoothly evolving simulated flight data.
 *
 *         Scenario: cruise climb from FL340 → FL370 heading West
 *         over the London TMA, then level-off.
 */
static void SimulatorThread() {
    SimState s;
    double elapsed_s = 0.0;

    // Prime the offset block before the server window even starts.
    {
        std::lock_guard<std::mutex> lk(g_OffsetMutex);
        FlushSimState(s);
    }

    while (g_Running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        elapsed_s += 0.5;

        // ── Slowly rotate heading (0.3 °/s, right-hand turn) ─────────────────
        s.heading_deg = fmod(s.heading_deg + 0.15, 360.0);

        // ── IAS oscillates gently around cruise speed ─────────────────────────
        s.ias_kts = 445.0 + 5.0 * std::sin(elapsed_s * 0.05);

        // ── Climb from FL340 to FL370 (≈ 10 363 m → 11 278 m) ────────────────
        if (s.alt_m < 11278.0) {
            s.vs_mps   = 2.54;                    // ≈ 500 fpm
            s.pitch_deg = 1.8;
            s.alt_m    += s.vs_mps * 0.5;         // advance 0.5 s worth
        } else {
            s.vs_mps    = 0.0;
            s.pitch_deg = 0.0;
        }

        // ── Aircraft moves westward at cruise speed ───────────────────────────
        // 240 m/s ≈ 0.002156 deg/s of longitude at 51° lat
        double lon_rate = (s.gs_mps / (111319.0 * std::cos(s.lat_deg * M_PI / 180.0)));
        s.lon_deg -= lon_rate * 0.5;   // westward → decreasing longitude

        {
            std::lock_guard<std::mutex> lk(g_OffsetMutex);
            FlushSimState(s);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IPC message handler
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Process one IPC batch from a client.
 *
 * @param wParam  Global atom whose name is the client's named file mapping.
 * @param lParam  Byte offset into that mapping where the packet chain starts
 *                (the SDK always passes 0 here).
 * @return        FS6IPC_MESSAGE_SUCCESS on success, FS6IPC_MESSAGE_FAILURE on error.
 */
static LRESULT HandleIPCMessage(WPARAM wParam, LPARAM lParam) noexcept {
    // ── 1.  Retrieve the file-mapping name stored in the global atom ──────────
    char szName[MAX_PATH] = {};
    if (GlobalGetAtomNameA(static_cast<ATOM>(wParam), szName, static_cast<int>(sizeof(szName))) == 0) {
        std::cerr << "[Server] ERROR  GlobalGetAtomName failed: " << GetLastError() << "\n";
        return FS6IPC_MESSAGE_FAILURE;
    }

    // ── 2.  Open the client's named file mapping ──────────────────────────────
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);
    if (!hMap) {
        std::cerr << "[Server] ERROR  OpenFileMapping(\"" << szName
                  << "\") failed: " << GetLastError() << "\n";
        return FS6IPC_MESSAGE_FAILURE;
    }

    // ── 3.  Map a view of the entire mapping ──────────────────────────────────
    auto* pBase = static_cast<uint8_t*>(
        MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!pBase) {
        std::cerr << "[Server] ERROR  MapViewOfFile failed: " << GetLastError() << "\n";
        CloseHandle(hMap);
        return FS6IPC_MESSAGE_FAILURE;
    }

    // ── 4.  Walk the packet chain (starting at lParam bytes into the view) ────
    uint8_t* p        = pBase + static_cast<size_t>(lParam);
    int      nReads   = 0;
    int      nWrites  = 0;
    bool     ok       = true;

    std::lock_guard<std::mutex> lk(g_OffsetMutex);

    while (true) {
        // Read the packet-type ID (first DWORD).
        uint32_t dwId = 0;
        std::memcpy(&dwId, p, sizeof(dwId));

        if (dwId == 0u) {
            break;   // ← Terminator: end of chain
        }

        // ── READ request ──────────────────────────────────────────────────────
        if (dwId == FS6IPC_READSTATEDATA_ID) {
            auto* hdr    = reinterpret_cast<IPC_ReadPacket*>(p);
            uint32_t off  = hdr->dwOffset;
            uint32_t nb   = hdr->nBytes;
            uint8_t* data = p + sizeof(IPC_ReadPacket);

            if (nb == 0u) {
                // Empty read – advance past header only.
                p += sizeof(IPC_ReadPacket);
                continue;
            }

            if (off + nb <= OFFSET_MEM_SIZE) {
                // Copy from our offset memory into the client mapping.
                std::memcpy(data, &g_OffsetMem[off], nb);
            } else {
                // Offset out of range — zero-fill so client gets deterministic data.
                std::memset(data, 0, nb);
                std::cerr << "[Server] WARN   READ  0x" << std::hex << off
                          << " +" << std::dec << nb << " B is out of range\n";
            }

            p += sizeof(IPC_ReadPacket) + nb;
            ++nReads;
        }

        // ── WRITE request ─────────────────────────────────────────────────────
        else if (dwId == FS6IPC_WRITESTATEDATA_ID) {
            auto* hdr    = reinterpret_cast<IPC_WritePacket*>(p);
            uint32_t off  = hdr->dwOffset;
            uint32_t nb   = hdr->nBytes;
            uint8_t* data = p + sizeof(IPC_WritePacket);

            if (nb > 0u) {
                if (off + nb <= OFFSET_MEM_SIZE) {
                    std::memcpy(&g_OffsetMem[off], data, nb);
                } else {
                    std::cerr << "[Server] WARN   WRITE 0x" << std::hex << off
                              << " +" << std::dec << nb << " B is out of range\n";
                }
            }

            p += sizeof(IPC_WritePacket) + nb;
            ++nWrites;
        }

        // ── Unknown / malformed ───────────────────────────────────────────────
        else {
            std::cerr << "[Server] ERROR  Unknown packet ID 0x"
                      << std::hex << dwId << std::dec
                      << " — aborting packet chain\n";
            ok = false;
            break;
        }
    }

    std::cout << "[Server] IPC OK  reads=" << nReads << "  writes=" << nWrites
              << "  map=\"" << szName << "\"\n";

    // ── 5.  Release the client mapping ───────────────────────────────────────
    UnmapViewOfFile(pBase);
    CloseHandle(hMap);

    return ok ? FS6IPC_MESSAGE_SUCCESS : FS6IPC_MESSAGE_FAILURE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Win32 window procedure
// ─────────────────────────────────────────────────────────────────────────────

static UINT g_ipcMsg = 0u;   ///< ID returned by RegisterWindowMessage

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam) {
    if (msg == g_ipcMsg) {
        // FS6IPC request from a client.
        return HandleIPCMessage(wParam, lParam);
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Console CTRL handler (Ctrl-C → graceful shutdown)
// ─────────────────────────────────────────────────────────────────────────────

static HWND g_hWnd = nullptr;

static BOOL WINAPI ConsoleCtrlHandler(DWORD) {
    g_Running = false;
    if (g_hWnd) PostMessageA(g_hWnd, WM_DESTROY, 0, 0);
    return TRUE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Startup banner
// ─────────────────────────────────────────────────────────────────────────────

static void PrintBanner() {
    // Table of all served offsets for the startup printout.
    static const struct { uint32_t off; uint32_t sz; const char* name; const char* units; }
    kOffsets[] = {
        { 0x3304, 4, "FSUIPC Version       ", "HIWORD=ver×1000, LOWORD=build"               },
        { 0x3308, 4, "FS Version / Validity", "0xFADE0000 | FS_type"                        },
        { 0x2380, 4, "Magnetic Heading     ", "deg x 65536/360              (DWORD)"        },
        { 0x02B4, 4, "Ground Speed         ", "m/s x 65536                  (DWORD)"        },
        { 0x02BC, 4, "Indicated Airspeed   ", "kts x 128                    (DWORD)"        },
        { 0x02C8, 4, "Vertical Speed       ", "m/s x 256                    (DWORD signed)" },
        { 0x0330, 2, "Altimeter Pressure   ", "mb x 16                      (WORD)"         },
        { 0x0366, 2, "On Ground            ", "0=air, 1=ground              (WORD)"         },
        { 0x0560, 8, "Latitude             ", "deg x(10001750/90 x 65536^2) (QWORD sgn)"    },
        { 0x0568, 8, "Longitude            ", "deg/180 x INT64_MAX          (QWORD sgn)"    },
        { 0x0570, 8, "Altitude MSL         ", "m x 65536                    (QWORD sgn)"    },
        { 0x0578, 4, "Pitch Angle          ", "deg x 65536^2/360            (DWORD signed)" },
    };

    std::cout <<
        "\n"
        "+======================================================================+\n"
        "|       FSUIPC IPC Server  -  10 Simulated Flight-Data Offsets        |\n"
        "+======================================================================+\n"
        "|  Protocol : FS6IPC  (RegisterWindowMessage(\"FsasmLib:IPC\"))         |\n"
        "|  Window   : UIPCMAIN  (FindWindowEx-discoverable, top-level)        |\n"
        "|  Scenario : Cruise climb FL340->FL370 outbound from London Heathrow |\n"
        "+----------+-------+--------------------------+----------------------+\n"
        "|  Offset  | Bytes | Name                     | Encoding             |\n"
        "+----------+-------+--------------------------+----------------------+\n";

    for (const auto& o : kOffsets) {
        std::cout
            << "|  0x" << std::hex << std::setw(4) << std::setfill('0') << o.off
            << std::dec << std::setfill(' ')
            << "  |   " << o.sz << "   | "
            << std::left << std::setw(24) << o.name << " | "
            << std::setw(20) << o.units << " |\n";
    }

    std::cout <<
        "+----------+-------+--------------------------+----------------------+\n"
        "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    PrintBanner();

    // ── Zero and initialise the offset block ─────────────────────────────────
    std::memset(g_OffsetMem, 0, sizeof(g_OffsetMem));
    {
        SimState init;
        std::lock_guard<std::mutex> lk(g_OffsetMutex);
        FlushSimState(init);
    }

    // ── Register the FS6IPC window message ───────────────────────────────────
    g_ipcMsg = RegisterWindowMessageA(FS6IPC_MSGNAME);
    if (g_ipcMsg == 0u) {
        std::cerr << "[Server] FATAL  RegisterWindowMessage failed: "
                  << GetLastError() << "\n";
        return 1;
    }
    std::cout << "[Server] Registered \"" << FS6IPC_MSGNAME
              << "\" -> msg ID 0x" << std::hex << g_ipcMsg << std::dec << "\n";

    // ── Register window class ─────────────────────────────────────────────────
    WNDCLASSA wc  = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = FSUIPC_WINDOW_CLASS;
    if (!RegisterClassA(&wc)) {
        std::cerr << "[Server] FATAL  RegisterClass failed: " << GetLastError() << "\n";
        return 1;
    }

    // ── Create a hidden top-level window with class & title "UIPCMAIN" ───────
    //    Must be top-level (not HWND_MESSAGE) so that clients can find it with:
    //      FindWindowEx(NULL, NULL, "UIPCMAIN", NULL)
    g_hWnd = CreateWindowExA(
        0,                          // no extended style
        FSUIPC_WINDOW_CLASS,        // class  = "UIPCMAIN"
        FSUIPC_WINDOW_CLASS,        // title  = "UIPCMAIN"
        WS_POPUP,                   // minimal style — no frame/caption
        0, 0, 1, 1,                 // 1×1 px; will be hidden below
        nullptr,                    // parent = desktop → top-level window
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );
    if (!g_hWnd) {
        std::cerr << "[Server] FATAL  CreateWindowEx failed: " << GetLastError() << "\n";
        return 1;
    }
    ShowWindow(g_hWnd, SW_HIDE);   // keep it invisible

    std::cout << "[Server] Window \"" << FSUIPC_WINDOW_CLASS
              << "\" created (HWND=0x"
              << std::hex << reinterpret_cast<uintptr_t>(g_hWnd) << std::dec
              << ") -- hidden, discoverable by class name\n";

    // ── Install Ctrl-C handler ───────────────────────────────────────────────
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // ── Start simulation thread ───────────────────────────────────────────────
    std::thread simThread(SimulatorThread);
    std::cout << "[Server] Simulation thread started.\n"
              << "[Server] Listening for IPC requests (\"" << FS6IPC_MSGNAME << "\")...\n"
              << "[Server] Press Ctrl-C to stop.\n\n";

    // ── Standard Win32 message loop ───────────────────────────────────────────
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // ── Graceful shutdown ─────────────────────────────────────────────────────
    g_Running = false;
    if (simThread.joinable()) simThread.join();

    DestroyWindow(g_hWnd);
    UnregisterClassA(FSUIPC_WINDOW_CLASS, GetModuleHandleA(nullptr));

    std::cout << "\n[Server] Shutdown complete.\n";
    return 0;
}
