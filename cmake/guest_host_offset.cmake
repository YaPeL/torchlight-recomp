# TORCHLIGHT_GUEST_HOST_OFFSET, for src/guest_abi/xbox_memory.h (kHostOffset): how much further up
# the host maps guest memory from 0xE0000000. The SDK adds 0x1000 there where the host's allocation
# granularity is coarser than 4 KB (rex::memory::detail::PhysicalHostOffset, rex/system/xmemory.h):
# Windows (64 KB) and macOS arm64 (16 KB pages); nothing on Linux. src/hooks/guest_copy.cpp
# fails the build if the two disagree.
if(WIN32 OR (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"))
    add_compile_definitions(TORCHLIGHT_GUEST_HOST_OFFSET=0x1000u)
else()
    add_compile_definitions(TORCHLIGHT_GUEST_HOST_OFFSET=0u)
endif()
