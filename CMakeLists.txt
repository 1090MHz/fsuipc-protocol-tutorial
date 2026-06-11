cmake_minimum_required(VERSION 3.16)
project(FSUIPC_IPC_Server CXX)

# ── Require C++17 ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Windows is required (FSUIPC uses Win32 IPC) ───────────────────────────────
if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only (Win32 IPC).")
endif()

# ── Shared header ─────────────────────────────────────────────────────────────
add_library(fsuipc_ipc INTERFACE)
target_include_directories(fsuipc_ipc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── Server executable ─────────────────────────────────────────────────────────
add_executable(fsuipc_server src/fsuipc_server.cpp)
target_link_libraries(fsuipc_server PRIVATE fsuipc_ipc user32 kernel32)

# ── Test-client executable ────────────────────────────────────────────────────
add_executable(fsuipc_test_client src/fsuipc_test_client.cpp)
target_link_libraries(fsuipc_test_client PRIVATE fsuipc_ipc user32 kernel32)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(fsuipc_server      PRIVATE /W4 /WX)
    target_compile_options(fsuipc_test_client PRIVATE /W4 /WX)
else()
    target_compile_options(fsuipc_server      PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(fsuipc_test_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Usage message ─────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== FSUIPC IPC Server  (Pete Dowson / FS6IPC protocol) ===")
message(STATUS "  1. Run fsuipc_server.exe   (keeps running, Ctrl-C to stop)")
message(STATUS "  2. Run fsuipc_test_client.exe  (polls 10 offsets × 10 times)")
message(STATUS "")
