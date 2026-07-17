#include <windows.h>

#include <cstdio>

int wmain()
{
    const HWND controller = FindWindowW(L"CodexQuotaTaskbar.Controller", nullptr);
    if (!controller)
    {
        std::printf("[FAIL] production controller not found\n");
        return 2;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(controller, &processId);
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        std::printf("[FAIL] unable to open production process\n");
        return 2;
    }
    const ULONGLONG started = GetTickCount64();
    if (!PostMessageW(controller, WM_CLOSE, 0, 0))
    {
        CloseHandle(process);
        std::printf("[FAIL] unable to post WM_CLOSE\n");
        return 1;
    }
    const DWORD wait = WaitForSingleObject(process, 5000);
    const ULONGLONG elapsed = GetTickCount64() - started;
    CloseHandle(process);
    const bool displayGone = FindWindowW(L"CodexQuotaTaskbar.Display", nullptr) == nullptr;
    if (wait != WAIT_OBJECT_0 || !displayGone)
    {
        std::printf("[FAIL] graceful exit exceeded limit or left display window elapsed_ms=%llu\n", elapsed);
        return 1;
    }
    std::printf("[PASS] graceful exit completed and display window disappeared elapsed_ms=%llu\n", elapsed);
    return 0;
}
