# FSUIPC Protocol Tutorial & Reference Implementation

[![Windows](https://img.shields.io/badge/platform-Windows-blue.svg)](https://www.microsoft.com/windows)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> **The complete guide to understanding and implementing the FS6IPC protocol for flight simulator IPC communication**

This repository provides a **production-ready reference implementation** of the FS6IPC protocol used by FSUIPC, complete with detailed documentation for developers building testing frameworks or flight simulator integration tools.

## 📝 Important: Terminology

**Don't confuse the two "6"s:**

- **FS6IPC** = The IPC protocol name (from Flight Simulator 2002, known as "FS6")
- **FSUIPC6** = Version 6 of the FSUIPC product (for MSFS 2020)
- **FSUIPC7** = Version 7 of the FSUIPC product (for MSFS 2024, by John Dowson)

**All versions of FSUIPC (1 through 7) use the same FS6IPC protocol.** This tutorial teaches that underlying protocol - knowledge that applies across 20+ years of flight simulation IPC, from FS2004 through MSFS 2024.

> **Note:** This is an educational implementation for learning and testing. For production use with real flight simulators, use the official [FSUIPC7](http://www.fsuipc.com/) by John Dowson.

## 🎯 What You'll Learn

- ✅ **Windows Shared Memory IPC** - CreateFileMapping, MapViewOfFile, and proper synchronization
- ✅ **FS6IPC Protocol** - Complete packet structure, message flow, and client/server handshake
- ✅ **FSUIPC Offset Encoding** - How flight data is encoded in the famous FSUIPC offset space
- ✅ **Thread-Safe Architecture** - Mutex protection, concurrent access patterns

## 📦 What's Included

```
├── CMakeLists.txt         # Build configuration
├── src/
│   ├── fsuipc_ipc.h           # Protocol definitions (packets, constants, offsets)
│   ├── fsuipc_server.cpp      # Complete server implementation with simulated data
│   └── fsuipc_test_client.cpp # Client implementation demonstrating all features
└── docs/
    ├── TUTORIAL.md            # Step-by-step IPC protocol walkthrough
    ├── PROTOCOL_FLOW.md       # Request/response mechanics explained
    ├── ARCHITECTURE.md        # Deep dive into design decisions
    ├── OFFSETS.md             # Complete offset encoding reference
    ├── CONTRIBUTING.md        # Contribution guidelines
    └── QUICK_REFERENCE.md     # One-page cheat sheet
```

## 🚀 Quick Start

### Build Requirements

- **Windows 10/11** (Win32 API required)
- **CMake 3.16+**
- **MSVC 2019+** or **MinGW-w64 GCC**
- **C++17 compiler**

### Build & Run

```powershell
# Configure
mkdir build && cd build
cmake ..

# Build
cmake --build . --config Release

# Run server (in one terminal)
.\Release\fsuipc_server.exe

# Run client (in another terminal)
.\Release\fsuipc_test_client.exe
```

### What Happens

The **server** creates a hidden window (`UIPCMAIN`) and simulates a cruise flight from London Heathrow, updating 10 flight data offsets every 500ms.

The **client** discovers the server, establishes IPC, and polls flight data 10 times, displaying heading, speed, position, and altitude.

## 📚 Documentation

| Document                               | Description                               |
| -------------------------------------- | ----------------------------------------- |
| [**TUTORIAL.md**](TUTORIAL.md)         | Step-by-step guide to the IPC protocol    |
| [**ARCHITECTURE.md**](ARCHITECTURE.md) | Design decisions and scalability analysis |
| [**OFFSETS.md**](OFFSETS.md)           | FSUIPC offset reference with encodings    |

## 🎓 Use Cases

### 🧪 For Testing FSUIPC Add-ons

Run the server to test FSUIPC-compatible applications **without a flight simulator**. Perfect for:

- Unit testing add-on software
- CI/CD pipelines
- Development without MSFS running
- Learning the protocol before deploying to real FSUIPC

> **Note:** This is a test/learning server only. For connecting to real flight simulators, use [official FSUIPC](http://www.fsuipc.com/).

### 📖 For Learning Windows IPC

Study a **real-world implementation** of:

- Named shared memory
- Window message-based RPC
- Thread synchronization
- Client-server architecture

## 🔑 Key Features

### Protocol Implementation

- ✅ **Complete FS6IPC protocol** - All packet types (read/write)
- ✅ **Window discovery** - RegisterWindowMessage + FindWindowEx pattern
- ✅ **Shared memory IPC** - Named file mappings with proper cleanup
- ✅ **Atomic operations** - GlobalAddAtom for mapping name registration

### Architecture Highlights

- ✅ **Thread-safe offset memory** - 64KB block with mutex protection
- ✅ **Scalable design** - Ready for 500+ offset expansion
- ✅ **Clean separation** - IPC layer independent of data source
- ✅ **Production patterns** - RAII, error handling, resource cleanup

### Simulated Offsets

| Offset | Data                     | Encoding                           |
| ------ | ------------------------ | ---------------------------------- |
| 0x3304 | FSUIPC Version           | HIWORD=ver×1000, LOWORD=build      |
| 0x3308 | FS Type + Validity Token | 0xFADE0000 \| FS_type              |
| 0x0238 | Magnetic Heading         | deg × 65536/360 (DWORD)            |
| 0x02B4 | Ground Speed             | m/s × 65536 (DWORD)                |
| 0x02BC | Indicated Airspeed       | kts × 128 (DWORD)                  |
| 0x02C8 | Vertical Speed           | m/s × 256 (signed DWORD)           |
| 0x0330 | Altimeter Pressure       | mb × 16 (WORD)                     |
| 0x0366 | On Ground Flag           | 0=air, 1=ground (WORD)             |
| 0x0560 | Latitude                 | Complex encoding (signed QWORD)    |
| 0x0568 | Longitude                | deg/180 × INT64_MAX (signed QWORD) |
| 0x0570 | Altitude MSL             | m × 65536 (signed QWORD)           |
| 0x0578 | Pitch Angle              | deg × 65536²/360 (signed DWORD)    |

**Full details**: [OFFSETS.md](OFFSETS.md)

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    FSUIPC IPC Server                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐      ┌──────────────────┐             │
│  │  Win32 Window    │      │ Simulator Thread │             │
│  │   "UIPCMAIN"     │      │  (Updates every  │             │
│  │                  │      │    500ms)        │             │
│  └────────┬─────────┘      └────────┬─────────┘             │
│           │                         │                       │
│           │ WM_COPYDATA            │ Writes                 │
│           ▼                         ▼                       │
│  ┌────────────────────────────────────────┐                 │
│  │   IPC Message Handler                  │                 │
│  │   • GlobalGetAtomName                  │                 │
│  │   • OpenFileMapping (client's memory)  │                 │
│  │   • Process read/write packets         │                 │
│  └──────────────┬─────────────────────────┘                 │
│                 │                                           │
│                 ▼                                           │
│  ┌────────────────────────────────────────┐                 │
│  │   64KB Offset Memory (g_OffsetMem)    │  ◄── Mutex       │
│  │   • Thread-safe with std::mutex        │   Protected     │
│  │   • All FSUIPC offsets stored here     │                 │
│  └────────────────────────────────────────┘                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │ Named Shared Memory
                            │ "FsasmLib:IPC:PID:N"
                            │
┌─────────────────────────────────────────────────────────────┐
│                    FSUIPC Test Client                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. FindWindowEx(NULL, NULL, "UIPCMAIN", NULL)              │
│  2. RegisterWindowMessage("FsasmLib:IPC")                   │
│  3. CreateFileMapping("FsasmLib:IPC:<PID>:<n>")             │
│  4. GlobalAddAtom(mapping name)                             │
│  5. Write request packets to shared memory                  │
│  6. SendMessageTimeout(hWnd, msg, atom, 0)                  │
│  7. Read response data from shared memory                   │
│  8. Cleanup: UnmapViewOfFile, CloseHandle                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 🔬 Code Walkthrough

### Server: Handling an IPC Request

```cpp
// 1. Get shared memory name from atom
char szName[MAX_PATH];
GlobalGetAtomNameA(static_cast<ATOM>(wParam), szName, sizeof(szName));

// 2. Open client's shared memory
HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);

// 3. Map into server address space
uint8_t* pBase = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

// 4. Walk packet chain
while (packet->dwId != 0) {
    if (packet->dwId == FS6IPC_READSTATEDATA_ID) {
        // Lock, copy from offset memory, unlock
        std::lock_guard<std::mutex> lk(g_OffsetMutex);
        memcpy(packet->data, &g_OffsetMem[packet->dwOffset], packet->nBytes);
    }
}

// 5. Cleanup
UnmapViewOfFile(pBase);
CloseHandle(hMap);
```

### Client: Making an IPC Call

```cpp
// 1. Create shared memory
HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                  PAGE_READWRITE, 0, BUFFER_SIZE, szName);
ATOM atom = GlobalAddAtomA(szName);
uint8_t* pView = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

// 2. Write request packets
auto* pkt = reinterpret_cast<IPC_ReadPacket*>(pView);
pkt->dwId = FS6IPC_READSTATEDATA_ID;
pkt->dwOffset = 0x0238;  // heading
pkt->nBytes = 4;

// 3. Send to server
DWORD_PTR result;
SendMessageTimeoutA(hWnd, ipcMsg, atom, 0, SMTO_BLOCK, 2000, &result);

// 4. Read response
uint32_t heading_raw = *reinterpret_cast<uint32_t*>(pkt->data);
double heading_deg = heading_raw * 360.0 / 65536.0;
```

## References

- [FSUIPC7 Official Site](http://www.fsuipc.com/) - Current FSUIPC for MSFS 2024
- [FSUIPC SDK](http://www.schiratti.com/dowson.html) - Pete Dowson's original SDK documentation
- [FSUIPC Offsets](http://fsuipcoffsets.com/) - Complete offset documentation
- [Win32 File Mapping](https://docs.microsoft.com/en-us/windows/win32/memory/file-mapping) - Microsoft documentation

## 🤝 Contributing

Contributions welcome! Areas of interest:

- Additional offset implementations
- Performance optimizations
- Documentation improvements
- Testing frameworks

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## 📄 License

MIT License - See [LICENSE](LICENSE) for details.

This is an independent educational implementation based on publicly documented protocol specifications. Not affiliated with or endorsed by the official FSUIPC project.

## 🙏 Acknowledgments

- **Pete Dowson** - Original FSUIPC design and protocol documentation
- **John Dowson** - Current FSUIPC7 maintainer
- **Adam Szofran** - Original FsasmLib and FS6IPC protocol (FS2002)

---

**Questions?** Open an issue or discussion. This project is meant to be educational—ask anything!
