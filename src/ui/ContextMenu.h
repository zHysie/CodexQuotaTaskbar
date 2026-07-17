#pragma once

#include "settings/Settings.h"

#include <windows.h>

namespace cqt
{

enum ContextCommand : UINT
{
    CommandRefresh = 1001,
    CommandLayoutVertical,
    CommandLayoutHorizontal,
    CommandShowFiveHour,
    CommandShowWeekly,
    CommandInterval60,
    CommandInterval180,
    CommandInterval300,
    CommandInterval600,
    CommandInterval1800,
    CommandColorSystem,
    CommandColorWhite,
    CommandColorBlack,
    CommandColorQuotaAware,
    CommandStartup,
    CommandRevalidate,
    CommandAbout,
    CommandExit
};

class ContextMenu
{
public:
    [[nodiscard]] static UINT Show(HWND owner, int screenX, int screenY,
                                   const SettingsData& settings, bool startupEnabled);
};

} // namespace cqt
