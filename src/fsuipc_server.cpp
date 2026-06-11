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
#include "fsuipc_offset_api.h"

#include <windows.h>
#include <iostream>
#include <iomanip>
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
// Simulation state updater
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Write the current SimState into g_OffsetMem using the offset table.
 * @note   Caller must hold g_OffsetMutex.
 */
static void FlushSimState(const SimState& s) noexcept {
    EncodeAllOffsets(s, g_OffsetMem);
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
        s.heading_deg = std::fmod(s.heading_deg + 0.15, 360.0);

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
    // Use the offset table from fsuipc_offsets.h
    size_t count;
    const OffsetEncoder* table = GetOffsetTable(count);

    std::cout <<
        "\n"
        "+======================================================================+\n"
        "|       FSUIPC IPC Server  -  " << count << " Simulated Flight-Data Offsets        |\n"
        "+======================================================================+\n"
        "|  Protocol : FS6IPC  (RegisterWindowMessage(\"FsasmLib:IPC\"))         |\n"
        "|  Window   : UIPCMAIN  (FindWindowEx-discoverable, top-level)        |\n"
        "|  Scenario : Cruise climb FL340->FL370 outbound from London Heathrow |\n"
        "|  Architecture: TABLE-DRIVEN (easily scales to 100+ offsets)         |\n"
        "+----------+-------+----------------------------------------------+\n"
        "|  Offset  | Bytes | Description                                  |\n"
        "+----------+-------+----------------------------------------------+\n";

    for (size_t i = 0; i < count; ++i) {
        std::cout
            << "|  0x" << std::hex << std::setw(4) << std::setfill('0') << table[i].offset
            << std::dec << std::setfill(' ')
            << "  |   " << table[i].size << "   | "
            << std::left << std::setw(44) << table[i].description << " |\n";
    }

    std::cout <<
        "+----------+-------+----------------------------------------------+\n"
        "\n"
        "To add more offsets: see fsuipc_offsets.h\n"
        "For scaling guidance: see docs/SCALING.md\n"
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
