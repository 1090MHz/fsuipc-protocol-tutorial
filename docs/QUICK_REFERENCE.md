# FS6IPC Protocol Quick Reference

> **One-page cheat sheet for FS6IPC protocol implementation**
> 
> **Note:** FS6IPC is the protocol name (from FS2002). All FSUIPC versions use this protocol - from FSUIPC1 through FSUIPC7.

## Protocol Constants

```cpp
#define FSUIPC_WINDOW_CLASS "UIPCMAIN"
#define FS6IPC_MSGNAME "FsasmLib:IPC"
#define FS6IPC_MESSAGE_SUCCESS 1u
#define FS6IPC_MESSAGE_FAILURE 0u
#define FS6IPC_READSTATEDATA_ID 1u
#define FS6IPC_WRITESTATEDATA_ID 2u
#define IPC_BUFFER_MAX_SIZE 0x7F00u
```

## Packet Structures

```cpp
#pragma pack(push, 1)

struct IPC_ReadPacket {
    uint32_t dwId;       // = 1
    uint32_t dwOffset;   // FSUIPC offset
    uint32_t nBytes;     // Size to read
    uint32_t pDest;      // Client pointer (informational)
    // Followed by nBytes of data
};  // 16 bytes

struct IPC_WritePacket {
    uint32_t dwId;       // = 2
    uint32_t dwOffset;   // FSUIPC offset
    uint32_t nBytes;     // Size to write
    // Followed by nBytes of data
};  // 12 bytes

#pragma pack(pop)
```

## Client Sequence

```cpp
// 1. Find server
HWND hWnd = FindWindowEx(NULL, NULL, "UIPCMAIN", NULL);

// 2. Get message ID
UINT msg = RegisterWindowMessage("FsasmLib:IPC");

// 3. Create shared memory
char name[256];
sprintf(name, "FsasmLib:IPC:%08X:1", GetCurrentProcessId());
ATOM atom = GlobalAddAtomA(name);
HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                  PAGE_READWRITE, 0, 32768, name);
BYTE* pView = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

// 4. Write packets
auto* pkt = reinterpret_cast<IPC_ReadPacket*>(pView);
pkt->dwId = FS6IPC_READSTATEDATA_ID;
pkt->dwOffset = 0x0238;
pkt->nBytes = 4;
*reinterpret_cast<uint32_t*>(pView + sizeof(IPC_ReadPacket) + 4) = 0; // Terminator

// 5. Send message
DWORD_PTR result;
SendMessageTimeoutA(hWnd, msg, atom, 0, SMTO_BLOCK, 2000, &result);

// 6. Read response
uint32_t heading = *reinterpret_cast<uint32_t*>(
    reinterpret_cast<BYTE*>(pkt) + sizeof(IPC_ReadPacket));

// 7. Cleanup
UnmapViewOfFile(pView);
CloseHandle(hMap);
GlobalDeleteAtom(atom);
```

## Server Sequence

```cpp
// 1. Register message
UINT g_ipcMsg = RegisterWindowMessageA("FsasmLib:IPC");

// 2. Create window
WNDCLASSA wc = {};
wc.lpfnWndProc = WndProc;
wc.lpszClassName = "UIPCMAIN";
RegisterClassA(&wc);
HWND hWnd = CreateWindowExA(0, "UIPCMAIN", "UIPCMAIN", WS_POPUP,
                              0, 0, 1, 1, NULL, NULL, NULL, NULL);
ShowWindow(hWnd, SW_HIDE);

// 3. Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_ipcMsg) {
        return HandleIPCMessage(wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 4. Handle IPC
DWORD HandleIPCMessage(WPARAM wParam, LPARAM lParam) {
    // Get mapping name from atom
    char szName[MAX_PATH];
    GlobalGetAtomNameA((ATOM)wParam, szName, sizeof(szName));

    // Open client's mapping
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);
    uint8_t* pBase = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    // Process packets
    uint8_t* p = pBase + lParam;
    while (*reinterpret_cast<uint32_t*>(p) != 0) {
        auto* pkt = reinterpret_cast<IPC_ReadPacket*>(p);
        if (pkt->dwId == FS6IPC_READSTATEDATA_ID) {
            memcpy(p + sizeof(IPC_ReadPacket),
                   &g_OffsetMem[pkt->dwOffset],
                   pkt->nBytes);
            p += sizeof(IPC_ReadPacket) + pkt->nBytes;
        }
        // Handle write packets...
    }

    // Cleanup
    UnmapViewOfFile(pBase);
    CloseHandle(hMap);
    return FS6IPC_MESSAGE_SUCCESS;
}
```

## Common Offsets & Encodings

| Offset | Size | Description    | Encoding                              |
| ------ | ---- | -------------- | ------------------------------------- |
| 0x3304 | 4    | FSUIPC Version | `HIWORD=ver×1000, LOWORD=build`       |
| 0x3308 | 4    | FS Version     | `0xFADE0000 \| FS_type`               |
| 0x0238 | 4    | Heading        | `deg × 65536 / 360`                   |
| 0x02B4 | 4    | Ground Speed   | `m/s × 65536`                         |
| 0x02BC | 4    | IAS            | `kts × 128`                           |
| 0x02C8 | 4    | Vertical Speed | `m/s × 256` (signed)                  |
| 0x0330 | 2    | Altimeter      | `mb × 16`                             |
| 0x0366 | 2    | On Ground      | `0=air, 1=ground`                     |
| 0x0560 | 8    | Latitude       | `deg × (10001750×65536²/90)` (signed) |
| 0x0568 | 8    | Longitude      | `deg × INT64_MAX / 180` (signed)      |
| 0x0570 | 8    | Altitude MSL   | `m × 65536` (signed)                  |
| 0x0578 | 4    | Pitch          | `deg × 65536² / 360` (signed)         |

## Encoding/Decoding Examples

```cpp
// Heading (0x0238)
uint32_t EncodeHeading(double deg) {
    return (uint32_t)(deg * 65536.0 / 360.0);
}
double DecodeHeading(uint32_t raw) {
    return raw * 360.0 / 65536.0;
}

// IAS (0x02BC)
uint32_t EncodeIAS(double kts) {
    return (uint32_t)(kts * 128.0);
}
double DecodeIAS(uint32_t raw) {
    return raw / 128.0;
}

// Latitude (0x0560)
int64_t EncodeLatitude(double deg) {
    return (int64_t)(deg * (10001750.0 * 65536.0 * 65536.0) / 90.0);
}
double DecodeLatitude(int64_t raw) {
    return raw * 90.0 / (10001750.0 * 65536.0 * 65536.0);
}

// Altitude (0x0570)
int64_t EncodeAltitude(double meters) {
    return (int64_t)(meters * 65536.0);
}
double DecodeAltitudeFt(int64_t raw) {
    return (raw / 65536.0) * 3.28084;
}
```

## Common Pitfalls

❌ **Integer overflow in encoding:**

```cpp
uint32_t raw = (heading * 65536) / 360;  // Wrong! Overflow
```

✅ **Use floating-point:**

```cpp
uint32_t raw = (uint32_t)(heading * 65536.0 / 360.0);
```

❌ **Forgetting #pragma pack(1):**

```cpp
struct IPC_ReadPacket { ... };  // May be 24 bytes on 64-bit!
```

✅ **Always use pack(1):**

```cpp
#pragma pack(push, 1)
struct IPC_ReadPacket { ... };  // Always 16 bytes
#pragma pack(pop)
```

❌ **Not checking GetLastError() immediately:**

```cpp
HANDLE h = CreateFile(...);
printf("Creating...\n");  // Other Win32 calls!
if (!h) printf("Error: %d\n", GetLastError());  // Wrong error!
```

✅ **Capture immediately:**

```cpp
HANDLE h = CreateFile(...);
DWORD err = GetLastError();
printf("Creating...\n");
if (!h) printf("Error: %d\n", err);
```

❌ **Leaking resources:**

```cpp
HANDLE h = CreateFileMapping(...);
if (error) return;  // Leak!
```

✅ **Always cleanup:**

```cpp
HANDLE h = CreateFileMapping(...);
if (error) {
    CloseHandle(h);
    return;
}
```

## Thread Safety Pattern

```cpp
// Shared data
static uint8_t g_OffsetMem[65536];
static std::mutex g_OffsetMutex;

// Writer thread
void UpdateThread() {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(g_OffsetMutex);
            // Write to g_OffsetMem
        }
        Sleep(500);
    }
}

// Reader (IPC handler)
void HandleIPC() {
    std::lock_guard<std::mutex> lock(g_OffsetMutex);
    // Read from g_OffsetMem
}
```

## Error Codes

| Function                | Success   | Failure |
| ----------------------- | --------- | ------- |
| `FindWindowEx`          | `!= NULL` | `NULL`  |
| `RegisterWindowMessage` | `!= 0`    | `0`     |
| `CreateFileMapping`     | `!= NULL` | `NULL`  |
| `OpenFileMapping`       | `!= NULL` | `NULL`  |
| `MapViewOfFile`         | `!= NULL` | `NULL`  |
| `GlobalAddAtom`         | `!= 0`    | `0`     |
| `SendMessageTimeout`    | `TRUE`    | `FALSE` |
| IPC Message Result      | `1`       | `0`     |

## Build Commands

```bash
# Configure
mkdir build && cd build
cmake ..

# Build (Windows/MSVC)
cmake --build . --config Release

# Build (MinGW)
cmake --build .

# Run
.\Release\fsuipc_server.exe      # Terminal 1
.\Release\fsuipc_test_client.exe # Terminal 2
```

## Useful Links

- **Full Tutorial:** [TUTORIAL.md](TUTORIAL.md)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
- **Offset Reference:** [OFFSETS.md](OFFSETS.md)
- **Official FSUIPC:** http://www.schiratti.com/dowson.html
- **Offset List:** http://fsuipcoffsets.com/

---

**Print this for reference!** Keep it handy while coding.
