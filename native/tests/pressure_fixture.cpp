#include <windows.h>
#include <cstdlib>

// Test-only replacement for the OS notification query. The normal host uses
// QueryMemoryResourceNotification directly and never opens the fixture file.
static BOOL WINAPI FixturePressure(HANDLE, PBOOL low) {
    const auto* flag = _wgetenv(L"YOMITSUGU_TEST_PRESSURE_FILE");
    if (!flag) return FALSE;
    *low = GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES;
    return TRUE;
}
#define QueryMemoryResourceNotification FixturePressure
#include "router.cpp"
