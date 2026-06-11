# FS6IPC Request/Response Protocol

> **Understanding the request-driven architecture: Why messages matter, what shared memory contains, and how data flows between client and server**
> 
> **Note:** FS6IPC is the protocol used by all FSUIPC versions. This document explains the core protocol mechanics.

## Table of Contents

1. [The Big Picture](#the-big-picture)
2. [Common Misconceptions](#common-misconceptions)
3. [Request/Response Lifecycle](#requestresponse-lifecycle)
4. [Message Purpose](#message-purpose)
5. [Shared Memory Contents](#shared-memory-contents)
6. [Read vs Write Operations](#read-vs-write-operations)
7. [Offset Access Control](#offset-access-control)
8. [Timing and Synchronization](#timing-and-synchronization)
9. [Performance Characteristics](#performance-characteristics)
10. [Code Examples](#code-examples)

---

## The Big Picture

**FSUIPC uses a REQUEST-DRIVEN model, not a broadcast/subscription model.**

```
┌─────────────────────────────────────────────────────────────────┐
│  WRONG MENTAL MODEL (not how FSUIPC works)                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Server continuously writes to ONE shared memory                │
│  ↓                                                              │
│  All clients read from same shared memory whenever they want    │
│  Problem: How do clients WRITE? How do they know data is fresh? │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  CORRECT MODEL (how FSUIPC actually works)                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Client creates its OWN shared memory                        │
│  2. Client writes request packets to its memory                 │
│  3. Client sends Windows message: "Process my requests!"        │
│  4. Server opens client's shared memory                         │
│  5. Server reads requests, processes them, writes responses     │
│  6. Server closes client's shared memory                        │
│  7. Client reads responses from its own shared memory           │
│                                                                 │
│  Each client has its own temporary shared memory!               │
│  Server maintains ONE internal 64KB offset memory (g_OffsetMem) │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Common Misconceptions

### ❌ Misconception #1: "Shared memory is continuously updated by the server"

**Reality:** The server maintains its own **internal** 64KB buffer (`g_OffsetMem`) that's continuously updated. Client's shared memory is only populated **on request** when the client sends a message.

```cpp
// Server's internal state (NOT accessible to clients directly)
static uint8_t g_OffsetMem[65536];  // Updated by SimulatorThread

// Client's temporary memory (created per-request)
HANDLE hMap = CreateFileMapping(...);  // Client owns this
BYTE* pView = MapViewOfFile(hMap, ...);
// ^ Server opens this ONLY when processing client's request
```

### ❌ Misconception #2: "The message just notifies; data flows automatically"

**Reality:** The message is the **trigger that causes all data transfer**. Without the message:

- Server doesn't know client wants data
- Server doesn't know which shared memory to open
- Server doesn't know what offsets to read/write
- NO data flows at all

### ❌ Misconception #3: "Clients can only read, not write"

**Reality:** FSUIPC is **bidirectional**:

- **READ**: Get current sim state (position, speed, etc.)
- **WRITE**: Control sim (set autopilot, change radios, trigger switches)

Many add-ons (autopilot panels, radio stacks, FMC units) **write** more than they read.

### ❌ Misconception #4: "All offsets are read-write"

**Reality:** Offsets have **access control**:

- **Read-only**: Position, altitudes, speeds (sim calculates these, you can't set them directly)
- **Read-write**: Autopilot settings, radio frequencies, fuel quantity, switch positions
- **Write-only**: Commands/events (toggle parking brake, reset sim, etc.)

---

## Request/Response Lifecycle

### Complete Sequence Diagram

```
CLIENT                               SERVER                    Internal State
  |                                    |                            |
  | 1. CreateFileMapping               |                            |
  |    "FsasmLib:IPC:12345678:1"       |                            |
  |----------------------------------->|                            |
  |                                    |                            |
  | 2. MapViewOfFile                   |                            |
  |    (get pointer to shared memory)  |                            |
  |----------------------------------->|                            |
  |                                    |                            |
  | 3. Write request packets           |                            |
  |    to shared memory:               |                            |
  |    [READ 0x0238, 4 bytes]          |                            |
  |    [WRITE 0x07DC, 4 bytes, 270]    |                            |
  |    [READ 0x0560, 8 bytes]          |                            |
  |    [TERMINATOR: 0]                 |                            |
  |----------------------------------->|                            |
  |                                    |                            |
  | 4. SendMessageTimeout              |                            |
  |    (BLOCKS waiting for response)   |                            |
  |====================================|                            |
  |                                    | 5. WndProc receives msg    |
  |                                    |--------------------------->|
  |                                    | 6. OpenFileMapping         |
  |                                    |    "FsasmLib:IPC:12345..."  |
  |                                    |--------------------------->|
  |                                    | 7. MapViewOfFile           |
  |                                    |    (server opens CLIENT's  |
  |                                    |     shared memory)         |
  |                                    |--------------------------->|
  |                                    |                            |
  |                                    | 8. Lock g_OffsetMutex      |
  |                                    |--------------------------->|
  |                                    |                            |
  |                                    | 9. Process packets:        |
  |                                    |    READ 0x0238:            |
  |                                    |      memcpy from           |
  |                                    |      g_OffsetMem[0x0238]   |
  |                                    |      to client's memory    |
  |                                    |                            |
  |                                    |    WRITE 0x07DC:           |
  |                                    |      memcpy from           |
  |                                    |      client's memory       |
  |                                    |      to g_OffsetMem[0x07DC]|
  |                                    |                            |
  |                                    |    READ 0x0560:            |
  |                                    |      memcpy from           |
  |                                    |      g_OffsetMem[0x0560]   |
  |                                    |      to client's memory    |
  |                                    |--------------------------->|
  |                                    |                            |
  |                                    | 10. Unlock g_OffsetMutex   |
  |                                    |--------------------------->|
  |                                    |                            |
  |                                    | 11. UnmapViewOfFile        |
  |                                    | 12. CloseHandle            |
  |                                    |--------------------------->|
  |                                    |                            |
  |                                    | 13. Return SUCCESS         |
  |<===================================|                            |
  | (SendMessageTimeout returns)       |                            |
  |                                    |                            |
  | 14. Read responses from            |                            |
  |     shared memory:                 |                            |
  |     heading = 087.5°               |                            |
  |     latitude = 51.477°N            |                            |
  |----------------------------------->|                            |
  |                                    |                            |
  | 15. UnmapViewOfFile                |                            |
  | 16. CloseHandle                    |                            |
  | 17. GlobalDeleteAtom               |                            |
  |----------------------------------->|                            |
  |                                    |                            |
```

### Key Observations

1. **Client BLOCKS** at step 4 until server finishes processing (synchronous)
2. **Server opens CLIENT's memory** (not the other way around)
3. **All packet processing happens in one atomic operation** (mutex held)
4. **Server never keeps client's memory open** (opens, processes, closes immediately)
5. **Client memory is temporary** (created per-request, destroyed after reading responses)

---

## Message Purpose

### What Does the Window Message Do?

The `SendMessageTimeout` call serves **multiple critical purposes**:

```cpp
DWORD_PTR result;
SendMessageTimeout(
    hWnd,              // 1. WHO: Which server to talk to
    g_ipcMsg,          // 2. WHAT: This is an IPC request
    atomName,          // 3. WHERE: Name of client's shared memory
    0,                 // 4. (unused in FSUIPC)
    SMTO_BLOCK,        // 5. HOW: Wait for completion
    2000,              // 6. WHEN: Timeout after 2 seconds
    &result            // 7. STATUS: Did it succeed?
);
```

**Why can't we skip the message?**

| Without Message                           | With Message                             |
| ----------------------------------------- | ---------------------------------------- |
| ❌ Server doesn't know client exists      | ✅ Server is notified                    |
| ❌ Server doesn't know memory name        | ✅ Memory name passed as atom            |
| ❌ Server doesn't know when to process    | ✅ Message triggers processing           |
| ❌ Client doesn't know when data is ready | ✅ Message blocks until done             |
| ❌ No error reporting                     | ✅ Return code indicates success/failure |

**The message is the contract:** "I've prepared requests in memory X, please process them NOW and tell me when you're done."

---

## Shared Memory Contents

### What's Actually In The Shared Memory?

The client's shared memory contains a **chain of request packets**, followed by **response data**.

#### Before Server Processing (Client Writes)

```
Offset  Content                                 Description
======  ======================================  ================================
0x0000  01 00 00 00                            dwId = 1 (READ)
0x0004  38 02 00 00                            dwOffset = 0x0238 (heading)
0x0008  04 00 00 00                            nBytes = 4
0x000C  XX XX XX XX                            pDest (client's pointer)
0x0010  [4 bytes reserved for response]        Server writes heading here

0x0014  02 00 00 00                            dwId = 2 (WRITE)
0x0018  DC 07 00 00                            dwOffset = 0x07DC (AP heading)
0x001C  04 00 00 00                            nBytes = 4
0x0020  00 C0 00 00                            Data: 270° × 65536/360 = 49152 (encoded)

0x0024  01 00 00 00                            dwId = 1 (READ)
0x0028  60 05 00 00                            dwOffset = 0x0560 (latitude)
0x002C  08 00 00 00                            nBytes = 8
0x0030  XX XX XX XX                            pDest (client's pointer)
0x0034  [8 bytes reserved for response]        Server writes latitude here

0x003C  00 00 00 00                            TERMINATOR (dwId = 0)
```

#### After Server Processing (Server Writes)

```
Offset  Content                                 Description
======  ======================================  ================================
0x0000  01 00 00 00                            dwId = 1 (READ)
0x0004  38 02 00 00                            dwOffset = 0x0238
0x0008  04 00 00 00                            nBytes = 4
0x000C  XX XX XX XX                            pDest
0x0010  00 E0 16 00                            ✅ Response: heading = 087.5°

0x0014  02 00 00 00                            dwId = 2 (WRITE)
0x0018  DC 07 00 00                            dwOffset = 0x07DC
0x001C  04 00 00 00                            nBytes = 4
0x0020  00 C0 00 00                            (unchanged - write request)

0x0024  01 00 00 00                            dwId = 1 (READ)
0x0028  60 05 00 00                            dwOffset = 0x0560
0x002C  08 00 00 00                            nBytes = 8
0x0030  XX XX XX XX                            pDest
0x0034  XX XX XX XX XX XX XX XX                ✅ Response: latitude data

0x003C  00 00 00 00                            TERMINATOR (dwId = 0)
```

### Memory Layout Rules

1. **Packets are chained** - One after another, no gaps
2. **Terminator required** - Server stops at `dwId == 0`
3. **Read packets expand** - Server writes response data immediately after header
4. **Write packets don't expand** - Data is already present
5. **Next packet starts** after previous packet's data
6. **Maximum size** - `IPC_BUFFER_MAX_SIZE` (32512 bytes typically)

---

## Read vs Write Operations

### Read Operation: Get Data From Simulator

```cpp
// Client requests current heading
IPC_ReadPacket* pkt = (IPC_ReadPacket*)pView;
pkt->dwId = FS6IPC_READSTATEDATA_ID;  // 1
pkt->dwOffset = 0x0238;                // Heading offset
pkt->nBytes = 4;                       // 4 bytes
pkt->pDest = 0;                        // Not used in this implementation

// Send request
SendMessageTimeout(...);

// Server copies from g_OffsetMem to client's memory
// Client reads response:
uint32_t* pResponse = (uint32_t*)((BYTE*)pkt + sizeof(IPC_ReadPacket));
uint32_t raw = *pResponse;
double heading = raw * 360.0 / 65536.0;
```

**Flow:**

```
Client Memory           Server g_OffsetMem
+-----------+           +-----------+
| READ 0x0238|    1.    | 0x0238:   |
| 4 bytes    |  Request | 0x15E00   |
|            |  ------>  | (heading) |
|            |    2.     |           |
| 0x15E00    | Response  |           |
|            | <-------  |           |
+-----------+           +-----------+
```

### Write Operation: Send Commands To Simulator

```cpp
// Client sets autopilot heading to 270°
IPC_WritePacket* pkt = (IPC_WritePacket*)pView;
pkt->dwId = FS6IPC_WRITESTATEDATA_ID;  // 2
pkt->dwOffset = 0x07DC;                 // AP heading bug
pkt->nBytes = 4;

// Encode 270° and write data after packet header
uint32_t encoded = (uint32_t)(270.0 * 65536.0 / 360.0);
*(uint32_t*)((BYTE*)pkt + sizeof(IPC_WritePacket)) = encoded;

// Send request
SendMessageTimeout(...);

// Server copies from client's memory to g_OffsetMem
```

**Flow:**

```
Client Memory           Server g_OffsetMem
+-----------+           +-----------+
| WRITE      |    1.    |           |
| 0x07DC     | Request  | 0x07DC:   |
| 4 bytes    |  ------>  | OLD_VALUE |
| 0x43870000 |    2.    |           |
|            |   Copy   | 0x07DC:   |
|            |  ------>  | 0x43870000|
+-----------+           +-----------+
```

### Batching Multiple Operations

**Efficiency win:** Client can batch many operations in one request!

```cpp
// Queue 10 read requests
QueueRead(0x0238, 4);  // Heading
QueueRead(0x02BC, 4);  // IAS
QueueRead(0x0560, 8);  // Latitude
QueueRead(0x0568, 8);  // Longitude
QueueRead(0x0570, 8);  // Altitude
// ... etc

// Process ALL at once with single message
Process();

// All responses are now in shared memory
```

**Performance:**

- ❌ 10 separate requests = 10 messages = ~100 µs
- ✅ 1 batched request = 1 message = ~10 µs

---

## Offset Access Control

### Read-Only Offsets

These represent **simulator state** that you can observe but not control:

```cpp
// Read-only: Simulator calculates these
0x0238  Heading (magnetic)         // Physics engine computes
0x0560  Latitude                   // Based on aircraft movement
0x0568  Longitude                  // Based on aircraft movement
0x0570  Altitude MSL               // Physics/terrain
0x02B4  Ground Speed               // Physics
0x02BC  IAS (Indicated Airspeed)   // Pitot system simulation
```

**Why read-only?** You can't just "set" your position or speed. The simulator's physics engine calculates these based on forces, controls, etc. Writing to these offsets would break physics.

**In a production implementation:**

```cpp
const std::unordered_set<uint32_t> READ_ONLY_OFFSETS = {
    0x0238, 0x0560, 0x0568, 0x0570, 0x02B4, 0x02BC, // ...
};

void HandleWritePacket(const IPC_WritePacket* pkt) {
    if (READ_ONLY_OFFSETS.count(pkt->dwOffset)) {
        std::cerr << "[Server] ERROR: Offset 0x" << std::hex
                  << pkt->dwOffset << " is read-only\n";
        return;  // Reject write
    }

    // Process write...
}
```

### Read-Write Offsets

These represent **controllable parameters**:

```cpp
// Read-write: You can set these
0x07DC  AP Heading Bug             // Set desired heading
0x07D4  AP Altitude                // Set desired altitude
0x07E2  AP Airspeed                // Set desired speed
0x0AF8  COM1 Radio Frequency       // Tune radio
0x0B46  NAV1 Radio Frequency       // Tune nav radio
0x0AF4  Transponder Code           // Set squawk code
0x0B4C  QNH (Altimeter setting)    // Set barometer
0x0AF0  Fuel Quantity Tank 1       // Set fuel level
```

**Why read-write?** These are **inputs** to the simulator or **settings** that you control via cockpit switches/knobs.

**Real-world example:**

```cpp
// External autopilot panel writes:
WriteOffset(0x07DC, EncodeHeading(270.0));  // "Turn to heading 270"

// Glass cockpit display reads:
uint32_t raw = ReadOffset(0x07DC);
double bug = DecodeHeading(raw);
DrawHeadingBug(bug);  // Show where AP wants to go
```

### Write-Only Offsets (Commands)

These trigger **actions** rather than storing state:

```cpp
// Write-only: Triggers/commands
0x3110  Parking Brake Toggle       // Press parking brake
0x3114  AP Master Toggle           // Turn AP on/off
0x3118  AP Heading Hold Toggle     // Toggle HDG mode
// ... many control offsets
```

**Why write-only?** These don't have a "value" - they trigger an event. Reading them is meaningless.

### Access Control Table

```cpp
enum class OffsetAccess { ReadOnly, ReadWrite, WriteOnly };

struct OffsetInfo {
    uint32_t offset;
    uint32_t size;
    OffsetAccess access;
    const char* description;
};

const OffsetInfo OFFSET_TABLE[] = {
    // Read-only: Aircraft state
    { 0x0238, 4, OffsetAccess::ReadOnly,  "Heading" },
    { 0x0560, 8, OffsetAccess::ReadOnly,  "Latitude" },

    // Read-write: Autopilot settings
    { 0x07DC, 4, OffsetAccess::ReadWrite, "AP Heading" },
    { 0x07D4, 4, OffsetAccess::ReadWrite, "AP Alt" },

    // Write-only: Commands
    { 0x3110, 4, OffsetAccess::WriteOnly, "Parking Brake", "sim/flight_controls/brakes_toggle_regular" },
};
```

---

## Timing and Synchronization

### Server's Internal Updates

The server **continuously** updates its internal offset memory:

```cpp
void SimulatorThread() {
    SimState state;

    while (g_Running) {
        // Update simulation state
        state.heading_deg += 0.3;
        state.altitude_m += 1.524;  // Climbing 500 fpm
        // ... update all parameters

        // Write to internal memory (locked)
        {
            std::lock_guard<std::mutex> lock(g_OffsetMutex);
            FlushSimState(state);  // Encode and write to g_OffsetMem
        }

        Sleep(500);  // 2 Hz update rate
    }
}
```

**Key point:** This happens **independently of client requests**. The server is always keeping its data fresh.

### Client's Request Timing

The client gets a **snapshot** of server state at the moment of the request:

```cpp
// T=0.0s: Heading = 87.0°
// T=0.5s: Heading = 87.3° (server updated)
// T=1.0s: Heading = 87.6° (server updated)
// T=1.5s: Client requests heading
//         → Receives 87.6° (current value at T=1.5s)
// T=2.0s: Heading = 87.9° (server updated)
// T=2.5s: Heading = 88.2° (server updated)
// T=3.0s: Client requests heading
//         → Receives 88.2° (current value at T=3.0s)
```

### Data Freshness Guarantee

**Because `SendMessageTimeout` blocks:**

- Client sends request at T=0
- Server receives at T+ε (microseconds later)
- Server locks mutex and reads g_OffsetMem
- **Data is from T+ε or later** (guaranteed fresh)
- Server writes responses
- Client receives at T+δ (typically T+10µs)

**Client always gets the most recent data available at the time of request.**

### Race Conditions (Prevented)

```cpp
// Potential race WITHOUT mutex:
Client requests heading at T=1.000s
    Server starts reading at T=1.001s
        SimThread writes at T=1.002s  // ⚠️ Partial read!
    Server finishes reading at T=1.003s

// Actual behavior WITH mutex:
Client requests heading at T=1.000s
    Server locks mutex at T=1.001s
        SimThread tries to write at T=1.002s
        → BLOCKED waiting for mutex
    Server reads g_OffsetMem[0x0238]
    Server unlocks mutex at T=1.003s
        → SimThread acquires lock and updates
```

**Mutex ensures atomic reads/writes** - no torn reads, no inconsistent state.

---

## Performance Characteristics

### Typical Latencies

| Operation              | Time       | Notes                       |
| ---------------------- | ---------- | --------------------------- |
| SendMessageTimeout     | ~10 µs     | Kernel-mode message passing |
| OpenFileMapping        | ~2 µs      | Open existing mapping       |
| MapViewOfFile          | ~3 µs      | Map into address space      |
| memcpy (4 bytes)       | <1 µs      | Single memory copy          |
| Mutex lock/unlock      | ~1 µs      | Uncontended                 |
| **Total per request**  | **~20 µs** | For 1 read operation        |
| **Batched (10 reads)** | **~30 µs** | Amortized overhead          |

### Scalability

**Single client polling 10 offsets at 10 Hz:**

```
10 offsets × 10 requests/sec = 100 operations/sec
Batched: 10 requests/sec × 30 µs = 0.3 ms/sec = 0.03% CPU
```

**10 clients polling 10 offsets at 10 Hz:**

```
10 clients × 10 requests/sec = 100 requests/sec
100 requests × 30 µs = 3 ms/sec = 0.3% CPU
```

**Bottleneck:** Window message queue is serialized

- Single-threaded message processing
- ~100,000 requests/sec max throughput
- For normal use (10-100 requests/sec), not an issue

### Optimization: Reduce Request Frequency

Instead of polling every frame (60 Hz):

```cpp
// ❌ Inefficient: 60 requests/sec
while (running) {
    float heading = ReadHeading();
    UpdateDisplay(heading);
    Sleep(16);  // 60 Hz
}

// ✅ Efficient: 10 requests/sec
while (running) {
    float heading = ReadHeading();

    for (int i = 0; i < 6; ++i) {  // Interpolate between samples
        UpdateDisplay(heading);  // Use same value
        Sleep(16);  // 60 Hz display update
    }

    Sleep(100);  // Request new data every 100ms
}
```

---

## Code Examples

### Example 1: Simple Read

```cpp
// Client: Read current heading
BYTE* pView = /* ... mapped view ... */;

// Write request packet
IPC_ReadPacket* pkt = (IPC_ReadPacket*)pView;
pkt->dwId = FS6IPC_READSTATEDATA_ID;
pkt->dwOffset = 0x0238;
pkt->nBytes = 4;
pkt->pDest = 0;

// Terminator
*(uint32_t*)(pView + sizeof(IPC_ReadPacket) + 4) = 0;

// Send request (BLOCKS until complete)
DWORD_PTR result;
SendMessageTimeout(hWnd, g_ipcMsg, atom, 0, SMTO_BLOCK, 2000, &result);

if (result == FS6IPC_MESSAGE_SUCCESS) {
    // Read response
    uint32_t* pData = (uint32_t*)(pView + sizeof(IPC_ReadPacket));
    double heading = (*pData) * 360.0 / 65536.0;
    std::cout << "Heading: " << heading << "°\n";
}
```

### Example 2: Simple Write

```cpp
// Client: Set autopilot heading to 270°
BYTE* pView = /* ... mapped view ... */;

// Write request packet
IPC_WritePacket* pkt = (IPC_WritePacket*)pView;
pkt->dwId = FS6IPC_WRITESTATEDATA_ID;
pkt->dwOffset = 0x07DC;  // AP Heading Bug
pkt->nBytes = 4;

// Write data
uint32_t encoded = (uint32_t)(270.0 * 65536.0 / 360.0);
*(uint32_t*)(pView + sizeof(IPC_WritePacket)) = encoded;

// Terminator
*(uint32_t*)(pView + sizeof(IPC_WritePacket) + 4) = 0;

// Send request
DWORD_PTR result;
SendMessageTimeout(hWnd, g_ipcMsg, atom, 0, SMTO_BLOCK, 2000, &result);

if (result == FS6IPC_MESSAGE_SUCCESS) {
    std::cout << "Autopilot heading set to 270°\n";
}
```

### Example 3: Batched Operations

```cpp
// Client: Read multiple offsets efficiently
BYTE* pView = /* ... mapped view ... */;
BYTE* p = pView;

// Request 1: Heading
IPC_ReadPacket* pkt1 = (IPC_ReadPacket*)p;
pkt1->dwId = FS6IPC_READSTATEDATA_ID;
pkt1->dwOffset = 0x0238;
pkt1->nBytes = 4;
pkt1->pDest = 0;
p += sizeof(IPC_ReadPacket) + 4;  // Move past response space

// Request 2: IAS
IPC_ReadPacket* pkt2 = (IPC_ReadPacket*)p;
pkt2->dwId = FS6IPC_READSTATEDATA_ID;
pkt2->dwOffset = 0x02BC;
pkt2->nBytes = 4;
pkt2->pDest = 0;
p += sizeof(IPC_ReadPacket) + 4;

// Request 3: Altitude
IPC_ReadPacket* pkt3 = (IPC_ReadPacket*)p;
pkt3->dwId = FS6IPC_READSTATEDATA_ID;
pkt3->dwOffset = 0x0570;
pkt3->nBytes = 8;
pkt3->pDest = 0;
p += sizeof(IPC_ReadPacket) + 8;

// Terminator
*(uint32_t*)p = 0;

// Send ONE request for all three
DWORD_PTR result;
SendMessageTimeout(hWnd, g_ipcMsg, atom, 0, SMTO_BLOCK, 2000, &result);

if (result == FS6IPC_MESSAGE_SUCCESS) {
    // Read all responses
    uint32_t heading_raw = *(uint32_t*)((BYTE*)pkt1 + sizeof(IPC_ReadPacket));
    uint32_t ias_raw = *(uint32_t*)((BYTE*)pkt2 + sizeof(IPC_ReadPacket));
    int64_t alt_raw = *(int64_t*)((BYTE*)pkt3 + sizeof(IPC_ReadPacket));

    double heading = heading_raw * 360.0 / 65536.0;
    double ias = ias_raw / 128.0;
    double alt_ft = (alt_raw / 65536.0) * 3.28084;

    std::cout << "Heading: " << heading << "°\n";
    std::cout << "IAS: " << ias << " kts\n";
    std::cout << "Altitude: " << alt_ft << " ft\n";
}
```

### Example 4: Server Processing (Simplified)

```cpp
// Server: Handle client request
DWORD HandleIPCMessage(WPARAM wParam, LPARAM lParam) {
    // Open client's shared memory
    char szName[MAX_PATH];
    GlobalGetAtomNameA((ATOM)wParam, szName, sizeof(szName));

    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, szName);
    if (!hMap) return FS6IPC_MESSAGE_FAILURE;

    uint8_t* pBase = (uint8_t*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!pBase) {
        CloseHandle(hMap);
        return FS6IPC_MESSAGE_FAILURE;
    }

    // Process packet chain
    uint8_t* p = pBase + lParam;

    while (*(uint32_t*)p != 0) {  // While not terminator
        uint32_t dwId = *(uint32_t*)p;

        if (dwId == FS6IPC_READSTATEDATA_ID) {
            auto* pkt = (IPC_ReadPacket*)p;

            // Copy from server's internal memory to client's memory
            {
                std::lock_guard<std::mutex> lock(g_OffsetMutex);
                memcpy(p + sizeof(IPC_ReadPacket),
                       &g_OffsetMem[pkt->dwOffset],
                       pkt->nBytes);
            }

            p += sizeof(IPC_ReadPacket) + pkt->nBytes;

        } else if (dwId == FS6IPC_WRITESTATEDATA_ID) {
            auto* pkt = (IPC_WritePacket*)p;

            // Copy from client's memory to server's internal memory
            {
                std::lock_guard<std::mutex> lock(g_OffsetMutex);
                memcpy(&g_OffsetMem[pkt->dwOffset],
                       p + sizeof(IPC_WritePacket),
                       pkt->nBytes);
            }

            p += sizeof(IPC_WritePacket) + pkt->nBytes;
        }
    }

    // Cleanup
    UnmapViewOfFile(pBase);
    CloseHandle(hMap);

    return FS6IPC_MESSAGE_SUCCESS;
}
```

---

## Summary

### Key Takeaways

1. **Request-driven model** - Data flows ONLY when client requests it
2. **Messages are essential** - They trigger processing and provide synchronization
3. **Each client has its own shared memory** - Created per-request, temporary
4. **Server maintains internal state** - The 64KB `g_OffsetMem` is continuously updated
5. **Bidirectional** - Clients can both READ (get data) and WRITE (send commands)
6. **Access control matters** - Not all offsets are writable
7. **Batching is efficient** - Multiple operations in one request
8. **Synchronous semantics** - Client knows exactly when data is ready

### Common Pitfalls

❌ Assuming shared memory is continuously updated  
❌ Thinking message is just a notification  
❌ Trying to access server's g_OffsetMem directly  
❌ Writing to read-only offsets  
❌ Not terminating packet chains  
❌ Sending too many individual requests instead of batching

### Next Steps

- **Read:** [TUTORIAL.md](TUTORIAL.md) for complete protocol walkthrough
- **Read:** [ARCHITECTURE.md](ARCHITECTURE.md) for design decisions
- **Read:** [OFFSETS.md](OFFSETS.md) for encoding specifications

---

**Questions?** Open an issue - this is a complex protocol and we want to make it crystal clear!
