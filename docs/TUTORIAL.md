# FS6IPC Protocol Tutorial

> **A complete step-by-step guide to understanding and implementing the FS6IPC protocol**

This tutorial walks through every aspect of the FS6IPC protocol (used by all FSUIPC versions), from basic Windows shared memory concepts to the complete client-server handshake. By the end, you'll understand exactly how flight simulator add-ons communicate with FSUIPC.

> **Note:** FS6IPC is the protocol name (from Flight Simulator 2002). All FSUIPC versions (1-7, including current FSUIPC7) use this same protocol. See [README](../README.md#-important-terminology) for terminology clarification.

## Table of Contents

1. [Windows IPC Fundamentals](#windows-ipc-fundamentals)
2. [The FS6IPC Protocol](#the-fs6ipc-protocol)
3. [Protocol Flow Walkthrough](#protocol-flow-walkthrough)
4. [Packet Structure Deep Dive](#packet-structure-deep-dive)
5. [Thread Safety & Synchronization](#thread-safety--synchronization)
6. [Error Handling Patterns](#error-handling-patterns)
7. [Performance Considerations](#performance-considerations)

---

## Windows IPC Fundamentals

### What is Shared Memory?

Shared memory allows multiple processes to access the **same physical memory region**. It's the fastest IPC mechanism because data doesn't need to be copied between processes—both simply read/write to the same memory.

### Key Windows Functions

#### Creating Shared Memory (Client Side)

```cpp
// 1. Create a named shared memory block
HANDLE hMap = CreateFileMappingA(
    INVALID_HANDLE_VALUE,      // Backed by system paging file (not a real file)
    nullptr,                   // Default security attributes
    PAGE_READWRITE,            // Read/write access
    0,                         // High-order DWORD of size (0 for <4GB)
    IPC_BUFFER_MAX_SIZE,       // Size in bytes
    "MySharedMemoryName"       // Global name for this mapping
);

// 2. Map it into this process's address space
BYTE* pView = static_cast<BYTE*>(
    MapViewOfFile(
        hMap,                  // Handle from CreateFileMapping
        FILE_MAP_ALL_ACCESS,   // Read/write access
        0, 0, 0                // Map entire file (all zeros = entire)
    )
);

// 3. Now pView points to the shared memory!
// Write data: pView[0] = 0x42;
// Read data:  uint8_t val = pView[0];
```

#### Opening Existing Shared Memory (Server Side)

```cpp
// 1. Open an already-created shared memory block by name
HANDLE hMap = OpenFileMappingA(
    FILE_MAP_ALL_ACCESS,       // Read/write access
    FALSE,                     // Don't inherit handle
    "MySharedMemoryName"       // Name created by client
);

// 2. Map it into this process's address space
BYTE* pView = static_cast<BYTE*>(
    MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0)
);

// 3. Both processes now see the SAME memory!
```

#### Cleanup (Both Sides)

```cpp
// Always cleanup in reverse order of creation
UnmapViewOfFile(pView);        // Unmap from address space
CloseHandle(hMap);             // Close the mapping handle
```

### Why Not Just Use Files?

You _could_ use regular files, but:

- ❌ **Slow**: Disk I/O is 1000x slower than memory
- ❌ **Synchronization issues**: File locking is complex
- ❌ **Unnecessary persistence**: Data disappears when process ends (desired!)

Shared memory backed by the **paging file** (`INVALID_HANDLE_VALUE`) is:

- ✅ **Fast**: RAM-speed access
- ✅ **Automatic cleanup**: OS frees it when all handles close
- ✅ **Simple**: No file system interaction

---

## The FS6IPC Protocol

### Protocol Overview

FSUIPC uses a **window message-based RPC** (Remote Procedure Call) pattern:

1. **Client** creates a named shared memory block
2. **Client** writes request packets into that memory
3. **Client** sends a window message to the server with the memory name
4. **Server** opens the client's shared memory
5. **Server** processes requests, writes responses back to the same memory
6. **Client** reads responses from its shared memory

### Why Window Messages?

Windows messages provide **synchronous RPC semantics**:

```cpp
// This BLOCKS until the server finishes processing!
SendMessageTimeout(hWndServer, ipcMsg, atom, 0, SMTO_BLOCK, 2000, &result);
```

When `SendMessageTimeout` returns, you **know**:

- ✅ The server received the message
- ✅ The server processed all requests
- ✅ Response data is ready in shared memory

### Protocol Discovery

Before IPC can happen, the client must find the server:

```cpp
// 1. Find the server's hidden window
HWND hWnd = FindWindowEx(NULL, NULL, "UIPCMAIN", NULL);
if (!hWnd) {
    // Server not running!
}

// 2. Get the registered IPC message ID
UINT ipcMsg = RegisterWindowMessage("FsasmLib:IPC");
// All processes calling RegisterWindowMessage with the same string
// get the SAME message ID (system-wide!)
```

This pattern is elegant:

- ✅ **No hardcoded ports** (unlike TCP/IP)
- ✅ **No registry** (unlike COM)
- ✅ **Automatic discovery** (window enumeration)

---

## Protocol Flow Walkthrough

Let's trace a complete IPC call step-by-step.

### Step 1: Client Setup (One-Time)

```cpp
// Find the server window
HWND s_hWnd = FindWindowEx(NULL, NULL, "UIPCMAIN", NULL);

// Get the IPC message ID
UINT s_Msg = RegisterWindowMessage("FsasmLib:IPC");

// Create a unique shared memory name
char szName[256];
sprintf(szName, "FsasmLib:IPC:%08X:1", GetCurrentProcessId());

// Register the name as a global atom (so we can pass it as WPARAM)
ATOM s_Atom = GlobalAddAtom(szName);

// Create the shared memory
HANDLE s_hMap = CreateFileMappingA(
    INVALID_HANDLE_VALUE,
    nullptr,
    PAGE_READWRITE,
    0,
    IPC_BUFFER_MAX_SIZE,
    szName
);

// Map it
BYTE* s_pView = MapViewOfFile(s_hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
```

### Step 2: Client Writes Request Packets

```cpp
BYTE* pNext = s_pView;  // Start at beginning

// Request 1: Read heading (offset 0x0238, 4 bytes)
auto* pkt1 = reinterpret_cast<IPC_ReadPacket*>(pNext);
pkt1->dwId = FS6IPC_READSTATEDATA_ID;  // = 1 (read request)
pkt1->dwOffset = 0x0238;                // FSUIPC offset for heading
pkt1->nBytes = 4;                       // 4 bytes
pkt1->pDest = reinterpret_cast<uint32_t>(&myHeadingVariable);  // (not used by server)
// After the header is space for data: pkt1->data[0..3]
pNext += sizeof(IPC_ReadPacket) + 4;

// Request 2: Read ground speed (offset 0x02B4, 4 bytes)
auto* pkt2 = reinterpret_cast<IPC_ReadPacket*>(pNext);
pkt2->dwId = FS6IPC_READSTATEDATA_ID;
pkt2->dwOffset = 0x02B4;
pkt2->nBytes = 4;
pkt2->pDest = reinterpret_cast<uint32_t>(&myGsVariable);
pNext += sizeof(IPC_ReadPacket) + 4;

// Terminator: DWORD of zero
*reinterpret_cast<uint32_t*>(pNext) = 0;
```

**Memory layout after this:**

```
Offset  | Content
--------+----------------------------------------------------------
0x0000  | 0x00000001 (dwId = READSTATEDATA)
0x0004  | 0x00000238 (dwOffset)
0x0008  | 0x00000004 (nBytes)
0x000C  | 0x???????? (pDest - client pointer, ignored by server)
0x0010  | [4 bytes of space for response data]
0x0014  | 0x00000001 (dwId = READSTATEDATA)
0x0018  | 0x000002B4 (dwOffset)
0x001C  | 0x00000004 (nBytes)
0x0020  | 0x???????? (pDest)
0x0024  | [4 bytes of space for response data]
0x0028  | 0x00000000 (terminator)
```

### Step 3: Client Sends Message

```cpp
DWORD_PTR dwResult = 0;
BOOL success = SendMessageTimeoutA(
    s_hWnd,          // Server window handle
    s_Msg,           // IPC message ID
    s_Atom,          // wParam = atom (server will use GlobalGetAtomName)
    0,               // lParam = offset into mapping (0 = start)
    SMTO_BLOCK,      // Block until server responds
    2000,            // 2 second timeout
    &dwResult        // Result: FS6IPC_MESSAGE_SUCCESS (1) or FAILURE (0)
);
```

**This blocks!** The client waits here while the server processes.

### Step 4: Server Receives Message

```cpp
// In the server's window procedure:
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_ipcMsg) {  // Our registered IPC message?
        return HandleIPCMessage(wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
```

### Step 5: Server Processes Request

```cpp
DWORD HandleIPCMessage(WPARAM wParam, LPARAM lParam) {
    // 1. Get the shared memory name from the atom
    char szName[MAX_PATH];
    GlobalGetAtomName(static_cast<ATOM>(wParam), szName, sizeof(szName));
    // szName is now "FsasmLib:IPC:00001234:1"

    // 2. Open the CLIENT's shared memory
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);

    // 3. Map into SERVER's address space
    uint8_t* pBase = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    // 4. Walk the packet chain starting at lParam offset
    uint8_t* p = pBase + static_cast<size_t>(lParam);

    while (true) {
        uint32_t dwId = *reinterpret_cast<uint32_t*>(p);

        if (dwId == 0) break;  // Terminator

        if (dwId == FS6IPC_READSTATEDATA_ID) {
            auto* pkt = reinterpret_cast<IPC_ReadPacket*>(p);

            // Lock our internal offset memory
            std::lock_guard<std::mutex> lock(g_OffsetMutex);

            // Copy from offset memory to packet data area
            uint8_t* pData = p + sizeof(IPC_ReadPacket);
            memcpy(pData, &g_OffsetMem[pkt->dwOffset], pkt->nBytes);

            p += sizeof(IPC_ReadPacket) + pkt->nBytes;
        }
        // else if (dwId == FS6IPC_WRITESTATEDATA_ID) { ... }
    }

    // 5. Cleanup
    UnmapViewOfFile(pBase);
    CloseHandle(hMap);

    return FS6IPC_MESSAGE_SUCCESS;  // = 1
}
```

**What just happened:**

- Server opened the **client's** shared memory
- Server read request packets
- Server wrote response data **into the client's memory**
- Server closed the client's memory

### Step 6: Client Reads Responses

```cpp
// SendMessageTimeout has returned, responses are ready!

// Read response from first packet
auto* pkt1 = reinterpret_cast<IPC_ReadPacket*>(s_pView);
uint32_t heading_raw = *reinterpret_cast<uint32_t*>(
    reinterpret_cast<BYTE*>(pkt1) + sizeof(IPC_ReadPacket)
);

// Decode FSUIPC format to degrees
double heading_deg = heading_raw * 360.0 / 65536.0;

// Read response from second packet
auto* pkt2 = reinterpret_cast<IPC_ReadPacket*>(
    s_pView + sizeof(IPC_ReadPacket) + 4
);
uint32_t gs_raw = *reinterpret_cast<uint32_t*>(
    reinterpret_cast<BYTE*>(pkt2) + sizeof(IPC_ReadPacket)
);

// Decode to knots
double gs_kts = (gs_raw / 65536.0) / 0.514444;
```

---

## Packet Structure Deep Dive

### Read Packet Layout

```cpp
struct IPC_ReadPacket {
    uint32_t dwId;       // = 1 (FS6IPC_READSTATEDATA_ID)
    uint32_t dwOffset;   // FSUIPC offset (e.g., 0x0238)
    uint32_t nBytes;     // Number of bytes to read
    uint32_t pDest;      // Client's destination pointer (informational only)
    // Followed by nBytes of data
};
```

**Size:** 16 bytes (with `#pragma pack(1)`)

**Example:**

```
dwId      = 0x00000001
dwOffset  = 0x00000238  (heading offset)
nBytes    = 0x00000004  (4 bytes)
pDest     = 0x00450120  (client's variable address)
[4 bytes] = 0x????????  (server fills this in)
```

### Write Packet Layout

```cpp
struct IPC_WritePacket {
    uint32_t dwId;       // = 2 (FS6IPC_WRITESTATEDATA_ID)
    uint32_t dwOffset;   // FSUIPC offset to write to
    uint32_t nBytes;     // Number of bytes to write
    // Followed by nBytes of data to write
};
```

**Size:** 12 bytes

**Example (set heading hold to 270°):**

```
dwId      = 0x00000002
dwOffset  = 0x000007CC  (autopilot heading hold)
nBytes    = 0x00000002  (2 bytes)
[2 bytes] = 0x010E      (270° in degrees)
```

### Why `#pragma pack(1)`?

```cpp
#pragma pack(push, 1)
struct IPC_ReadPacket {
    uint32_t dwId;      // Offset 0
    uint32_t dwOffset;  // Offset 4
    uint32_t nBytes;    // Offset 8
    uint32_t pDest;     // Offset 12
};
#pragma pack(pop)
```

Without `pack(1)`, compilers may add **padding** for alignment:

- ❌ With default packing: might be 24 bytes (8-byte alignment)
- ✅ With `pack(1)`: exactly 16 bytes

This is **critical** for binary compatibility between client and server!

---

## Thread Safety & Synchronization

### The Problem

The server has **two threads** accessing `g_OffsetMem`:

1. **Simulator Thread**: Updates offsets every 500ms
2. **IPC Handler**: Reads/writes offsets during client requests

Without synchronization, **data races** occur:

```
Time | Simulator Thread        | IPC Handler
-----+-------------------------+------------------------
T0   | Read heading = 270      |
T1   |                         | Read heading = 270 ✓
T2   | heading = 270.5         |
T3   | Write 270.5 to memory   |
T4   |                         | Read heading = 270.5 ✓ (or 270??)
```

### The Solution: Mutex

```cpp
static std::mutex g_OffsetMutex;

// In simulator thread:
void SimulatorThread() {
    while (running) {
        SimState newState = CalculateNewState();

        {
            std::lock_guard<std::mutex> lock(g_OffsetMutex);
            FlushSimState(newState);  // Write to g_OffsetMem
        }  // lock released automatically

        Sleep(500);
    }
}

// In IPC handler:
void HandleIPCMessage(...) {
    // ...
    if (packet->dwId == FS6IPC_READSTATEDATA_ID) {
        std::lock_guard<std::mutex> lock(g_OffsetMutex);
        memcpy(dest, &g_OffsetMem[offset], nBytes);  // Read from g_OffsetMem
    }  // lock released
    // ...
}
```

**How it works:**

- `lock_guard` acquires the mutex in its constructor
- If another thread holds the mutex, this thread **blocks** (waits)
- When the other thread releases (destructor), this thread acquires
- `lock_guard` **automatically releases** in its destructor (even if exception thrown!)

### Why Not Lock the Shared Memory?

The **shared memory** (client ↔ server) doesn't need a mutex because:

- ✅ **Synchronous protocol**: `SendMessageTimeout` blocks until server finishes
- ✅ **One request at a time**: Only one thread (the window message loop) processes IPC
- ✅ **Short-lived**: Shared memory only exists during the IPC call

The **offset memory** (`g_OffsetMem`) needs a mutex because:

- ❌ **Long-lived**: Exists for the entire server lifetime
- ❌ **Multiple accessors**: Simulator thread + IPC thread
- ❌ **Concurrent access**: Updates happen while IPC requests are processed

---

## Error Handling Patterns

### Resource Cleanup with RAII

```cpp
// ❌ BAD: Manual cleanup (easy to forget, leak on exception)
HANDLE hMap = CreateFileMapping(...);
BYTE* pView = MapViewOfFile(hMap, ...);
DoWork(pView);
UnmapViewOfFile(pView);
CloseHandle(hMap);

// ✅ GOOD: RAII wrapper (automatic cleanup)
class SharedMemoryView {
    HANDLE hMap_;
    BYTE* pView_;

public:
    SharedMemoryView(const char* name, size_t size) {
        hMap_ = CreateFileMapping(...);
        if (!hMap_) throw std::runtime_error("CreateFileMapping failed");

        pView_ = MapViewOfFile(hMap_, ...);
        if (!pView_) {
            CloseHandle(hMap_);
            throw std::runtime_error("MapViewOfFile failed");
        }
    }

    ~SharedMemoryView() {
        if (pView_) UnmapViewOfFile(pView_);
        if (hMap_) CloseHandle(hMap_);
    }

    BYTE* get() const { return pView_; }
};

// Usage:
SharedMemoryView view("FsasmLib:IPC:...", 4096);
DoWork(view.get());  // Automatic cleanup when view goes out of scope
```

### Error Code Checking

```cpp
// ❌ BAD: Ignoring errors
CreateFileMapping(...);  // What if this fails?
MapViewOfFile(...);      // What if this fails?

// ✅ GOOD: Check every fallible operation
HANDLE hMap = CreateFileMapping(...);
if (!hMap) {
    std::cerr << "CreateFileMapping failed: " << GetLastError() << "\n";
    return false;
}

BYTE* pView = MapViewOfFile(hMap, ...);
if (!pView) {
    std::cerr << "MapViewOfFile failed: " << GetLastError() << "\n";
    CloseHandle(hMap);  // Don't leak!
    return false;
}
```

### GetLastError() Pitfalls

```cpp
// ❌ BAD: GetLastError() after other calls
HANDLE hMap = CreateFileMapping(...);
std::cout << "Creating map...\n";  // This might call Windows functions!
if (!hMap) {
    // GetLastError() here might return error from cout, not CreateFileMapping!
    std::cerr << "Error: " << GetLastError() << "\n";
}

// ✅ GOOD: Capture error immediately
HANDLE hMap = CreateFileMapping(...);
DWORD lastError = GetLastError();  // Capture NOW
std::cout << "Creating map...\n";  // Safe now
if (!hMap) {
    std::cerr << "Error: " << lastError << "\n";  // Use captured value
}
```

---

## Performance Considerations

### Shared Memory vs Alternatives

| Method        | Latency   | Bandwidth    | Complexity |
| ------------- | --------- | ------------ | ---------- |
| Shared Memory | **~1 µs** | **~10 GB/s** | Medium     |
| Named Pipes   | ~50 µs    | ~100 MB/s    | Low        |
| TCP Sockets   | ~100 µs   | ~1 GB/s      | Medium     |
| UDP Sockets   | ~50 µs    | ~1 GB/s      | High       |
| Files         | ~1 ms     | ~500 MB/s    | Low        |

For flight sim IPC (small payloads, low latency), shared memory is **optimal**.

### Minimizing Lock Contention

```cpp
// ❌ BAD: Hold lock during encoding
{
    std::lock_guard<std::mutex> lock(g_OffsetMutex);
    double heading = state.heading_deg;
    double altitude = state.altitude_m;
    // ... read 50 more values ...

    // Encode (CPU-intensive)
    uint32_t heading_raw = static_cast<uint32_t>(heading * 65536.0 / 360.0);
    int64_t alt_raw = static_cast<int64_t>(altitude * 65536.0);
    // ... encode 50 more values ...

    // Write
    WriteOff(0x0238, heading_raw);
    WriteOff(0x0570, alt_raw);
}

// ✅ GOOD: Encode outside lock
double heading = state.heading_deg;
double altitude = state.altitude_m;
// ... read 50 more values (no lock yet!) ...

// Encode (no lock needed - local variables)
uint32_t heading_raw = static_cast<uint32_t>(heading * 65536.0 / 360.0);
int64_t alt_raw = static_cast<int64_t>(altitude * 65536.0);
// ... encode 50 more values ...

// Quick write (only this needs the lock)
{
    std::lock_guard<std::mutex> lock(g_OffsetMutex);
    WriteOff(0x0238, heading_raw);
    WriteOff(0x0570, alt_raw);
    // ... write 50 values ...
}  // Lock held for minimal time
```

### Batch Operations

```cpp
// ❌ BAD: 10 IPC calls for 10 values
for (int i = 0; i < 10; ++i) {
    QueueRead(offsets[i], sizes[i], &dest[i]);
    Process();  // Sends message to server
}
// Total time: 10 × (1 µs IPC + 10 µs message overhead) = 110 µs

// ✅ GOOD: 1 IPC call for 10 values
for (int i = 0; i < 10; ++i) {
    QueueRead(offsets[i], sizes[i], &dest[i]);
}
Process();  // Sends ONE message with all 10 requests
// Total time: 1 × (1 µs IPC + 10 µs overhead) = 11 µs
```

This is **exactly** what the test client does—queue all reads, then call `Process()` once.

---

## Next Steps

✅ **You now understand:**

- Windows shared memory IPC
- The complete FS6IPC protocol flow
- Packet structures and encoding
- Thread safety with mutexes
- Error handling best practices
- Performance optimization

📚 **Continue Learning:**

- [ARCHITECTURE.md](ARCHITECTURE.md) - Design decisions and scalability
- [OFFSETS.md](OFFSETS.md) - Complete offset reference

💻 **Try Coding:**

1. Modify the server to add a new simulated offset
2. Update the client to read your new offset
3. Implement a write request in the client
4. Add a new encoding/decoding function

**Questions?** Open an issue—this tutorial is meant to be interactive!
