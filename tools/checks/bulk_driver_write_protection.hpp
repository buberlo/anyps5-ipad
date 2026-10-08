#pragma once
// Test-only syscall interposition for the actual GuestArena translation unit.
// All ordinary calls delegate to the real Windows API; selected calls fail
// deterministically to exercise transactional scope rollback.
#include <windows.h>
extern "C" BOOL WINAPI BulkWriteTestVirtualProtect(void*, SIZE_T, DWORD, PDWORD);
#define VirtualProtect BulkWriteTestVirtualProtect
