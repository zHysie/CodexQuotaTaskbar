#include "taskbar/TaskbarHost.h"

#include <fcntl.h>
#include <io.h>
#include <objbase.h>
#include <windows.h>

#include <iostream>

int wmain()
{
    _setmode(_fileno(stdout), _O_U8TEXT);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        && GetLastError() != ERROR_ACCESS_DENIED)
    {
        std::wcerr << L"Unable to initialize Per-Monitor DPI Awareness V2.\n";
        return 1;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        std::wcerr << L"Unable to initialize COM. HRESULT=0x" << std::hex << comResult << L"\n";
        return 1;
    }

    const std::wstring report = cqt::TaskbarHost::BuildEnvironmentReport();
    std::wcout << report;

    cqt::TaskbarHost host;
    const bool supported = host.ProbeCompatibility().supported;
    CoUninitialize();
    return supported ? 0 : 2;
}
