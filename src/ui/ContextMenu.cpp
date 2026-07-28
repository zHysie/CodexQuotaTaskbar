#include "ui/ContextMenu.h"

namespace
{

void AddChecked(HMENU menu, UINT command, const wchar_t* text, bool checked, bool enabled = true)
{
    const UINT flags = MF_STRING
        | (checked ? MF_CHECKED : 0)
        | (enabled ? 0 : MF_GRAYED);
    AppendMenuW(menu, flags, command, text);
}

} // namespace

namespace cqt
{

UINT ContextMenu::Show(HWND owner, int screenX, int screenY,
                       const SettingsData& settings, bool startupEnabled)
{
    HMENU root = CreatePopupMenu();
    HMENU layout = CreatePopupMenu();
    HMENU content = CreatePopupMenu();
    HMENU interval = CreatePopupMenu();
    HMENU color = CreatePopupMenu();
    if (!root || !layout || !content || !interval || !color)
    {
        if (root) DestroyMenu(root);
        if (layout) DestroyMenu(layout);
        if (content) DestroyMenu(content);
        if (interval) DestroyMenu(interval);
        if (color) DestroyMenu(color);
        return 0;
    }
    AppendMenuW(root, MF_STRING, CommandRefresh, L"立即刷新");
    AddChecked(layout, CommandLayoutVertical, L"上下两行", settings.layout == LayoutMode::Vertical);
    AddChecked(layout, CommandLayoutHorizontal, L"单行", settings.layout == LayoutMode::Horizontal);
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(layout), L"显示模式");
    AddChecked(content, CommandShowFiveHour, L"显示 5 小时额度", settings.showFiveHour);
    AddChecked(content, CommandShowWeekly, L"显示周额度", settings.showWeekly);
    AddChecked(content, CommandShowSingleQuotaLabel, L"单项时显示标识（5h / 1W）",
               settings.showSingleQuotaLabel, CanToggleSingleQuotaLabel(settings));
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(content), L"显示内容");
    AddChecked(interval, CommandInterval60, L"1 分钟", settings.refreshIntervalSeconds == 60);
    AddChecked(interval, CommandInterval180, L"3 分钟", settings.refreshIntervalSeconds == 180);
    AddChecked(interval, CommandInterval300, L"5 分钟", settings.refreshIntervalSeconds == 300);
    AddChecked(interval, CommandInterval600, L"10 分钟", settings.refreshIntervalSeconds == 600);
    AddChecked(interval, CommandInterval1800, L"30 分钟", settings.refreshIntervalSeconds == 1800);
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(interval), L"刷新间隔");
    AddChecked(color, CommandColorSystem, L"跟随系统颜色", settings.colorMode == ColorMode::System);
    AddChecked(color, CommandColorWhite, L"固定白色", settings.colorMode == ColorMode::White);
    AddChecked(color, CommandColorBlack, L"固定黑色", settings.colorMode == ColorMode::Black);
    AddChecked(color, CommandColorQuotaAware, L"根据额度变化", settings.colorMode == ColorMode::QuotaAware);
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(color), L"颜色");
    AddChecked(root, CommandStartup, L"开机启动", startupEnabled);
    AppendMenuW(root, MF_STRING, CommandRevalidate, L"重新检测任务栏");
    AppendMenuW(root, MF_STRING, CommandAbout, L"关于");
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, CommandExit, L"退出");

    HWND previousForeground = GetForegroundWindow();
    SetForegroundWindow(owner);
    const UINT command = TrackPopupMenuEx(root, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                           screenX, screenY, owner, nullptr);
    DestroyMenu(root);
    PostMessageW(owner, WM_NULL, 0, 0);
    if (previousForeground && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    return command;
}

} // namespace cqt
