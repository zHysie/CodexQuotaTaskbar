cmake_minimum_required(VERSION 3.25)

project(CodexQuotaTaskbar VERSION 0.1.3 LANGUAGES CXX RC)

enable_testing()

set(CQT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CQT_GENERATED_DIR}")
file(TO_CMAKE_PATH "${CQT_GENERATED_DIR}/app.manifest" CQT_MANIFEST_RC_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico" CQT_ICON_RC_PATH)
configure_file(src/AppVersion.h.in "${CQT_GENERATED_DIR}/AppVersion.h" @ONLY)
configure_file(src/app.manifest.in "${CQT_GENERATED_DIR}/app.manifest" @ONLY)
configure_file(src/app.rc.in "${CQT_GENERATED_DIR}/app.rc" @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/package-version.txt" "${PROJECT_VERSION}\n")
set(CQT_GENERATED_RC "${CQT_GENERATED_DIR}/app.rc")

if(NOT WIN32)
    message(FATAL_ERROR "CodexQuotaTaskbar only supports Windows.")
endif()

add_library(data_core STATIC
    src/common/JsonAdapter.cpp
    src/common/JsonAdapter.h
    src/common/StringUtils.cpp
    src/common/StringUtils.h
    src/usage/CodexAuthReader.cpp
    src/usage/CodexAuthReader.h
    src/usage/CodexUsageClient.cpp
    src/usage/CodexUsageClient.h
    src/usage/AuthFileWatcher.cpp
    src/usage/AuthFileWatcher.h
    src/usage/HttpPolicy.cpp
    src/usage/HttpPolicy.h
    src/usage/HttpTransport.h
    src/usage/ResetCreditsParser.cpp
    src/usage/ResetCreditsParser.h
    src/usage/RefreshController.cpp
    src/usage/RefreshController.h
    src/usage/UsageModels.h
    src/usage/UsageParser.cpp
    src/usage/UsageParser.h
    src/usage/WinHttpTransport.cpp
    src/usage/WinHttpTransport.h
)

target_compile_features(data_core PUBLIC cxx_std_20)
target_compile_definitions(data_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_include_directories(data_core PUBLIC src PRIVATE third_party)
target_link_libraries(data_core PUBLIC winhttp)

add_library(taskbar_core STATIC
    src/taskbar/TaskbarCollision.h
    src/taskbar/TaskbarHost.cpp
    src/taskbar/TaskbarHost.h
)

target_compile_features(taskbar_core PUBLIC cxx_std_20)
target_compile_definitions(taskbar_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_include_directories(taskbar_core PUBLIC src)
target_link_libraries(taskbar_core PUBLIC advapi32 ole32 uiautomationcore user32)

add_executable(CodexQuotaTaskbar WIN32
    src/app_main.cpp
    src/App.cpp
    src/App.h
    src/common/SingleInstanceGuard.cpp
    src/common/SingleInstanceGuard.h
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/settings/StartupManager.cpp
    src/settings/StartupManager.h
    src/taskbar/TaskbarWindow.cpp
    src/taskbar/TaskbarWindow.h
    src/taskbar/TooltipController.cpp
    src/taskbar/TooltipController.h
    src/ui/ContextMenu.cpp
    src/ui/ContextMenu.h
    src/ui/QuotaLayout.h
    src/ui/TaskbarPresentation.cpp
    src/ui/TaskbarPresentation.h
    src/ui/TaskbarRenderer.cpp
    src/ui/TaskbarRenderer.h
    src/ui/TooltipText.cpp
    src/ui/TooltipText.h
    ${CQT_GENERATED_RC}
)
target_compile_features(CodexQuotaTaskbar PRIVATE cxx_std_20)
target_compile_definitions(CodexQuotaTaskbar PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_include_directories(CodexQuotaTaskbar PRIVATE src "${CQT_GENERATED_DIR}")
target_link_libraries(CodexQuotaTaskbar PRIVATE
    data_core taskbar_core advapi32 comctl32 d2d1 d3d11 dcomp dwrite dwmapi dxgi gdi32 ole32 shcore user32 winhttp
)

add_executable(CodexQuotaTaskbarPrototype WIN32
    src/main.cpp
    src/ui/QuotaLayout.h
    src/ui/TaskbarRenderer.cpp
    src/ui/TaskbarRenderer.h
    ${CQT_GENERATED_RC}
)

target_compile_features(CodexQuotaTaskbarPrototype PRIVATE cxx_std_20)
target_compile_definitions(CodexQuotaTaskbarPrototype PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_include_directories(CodexQuotaTaskbarPrototype PRIVATE src)
target_link_libraries(CodexQuotaTaskbarPrototype PRIVATE
    taskbar_core
    advapi32
    comctl32
    d2d1
    d3d11
    dcomp
    dwrite
    dwmapi
    dxgi
    gdi32
    ole32
    shcore
    user32
)

add_executable(TaskbarProbe
    src/probe_main.cpp
    ${CQT_GENERATED_RC}
)

target_compile_features(TaskbarProbe PRIVATE cxx_std_20)
target_compile_definitions(TaskbarProbe PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_include_directories(TaskbarProbe PRIVATE src)
target_link_libraries(TaskbarProbe PRIVATE taskbar_core ole32 user32)

add_executable(TaskbarInteractionProbe
    src/interaction_probe.cpp
    ${CQT_GENERATED_RC}
)

target_compile_features(TaskbarInteractionProbe PRIVATE cxx_std_20)
target_compile_definitions(TaskbarInteractionProbe PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_link_libraries(TaskbarInteractionProbe PRIVATE comctl32 user32)

add_executable(AppLifecycleProbe
    src/lifecycle_probe.cpp
    ${CQT_GENERATED_RC}
)
target_compile_features(AppLifecycleProbe PRIVATE cxx_std_20)
target_compile_definitions(AppLifecycleProbe PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_link_libraries(AppLifecycleProbe PRIVATE user32)

add_executable(TaskbarStabilityProbe
    src/stability_probe.cpp
    ${CQT_GENERATED_RC}
)
target_compile_features(TaskbarStabilityProbe PRIVATE cxx_std_20)
target_compile_definitions(TaskbarStabilityProbe PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_link_libraries(TaskbarStabilityProbe PRIVATE user32 shell32)

add_executable(QuotaLayoutTests
    tests/QuotaLayoutTests.cpp
    src/ui/QuotaLayout.h
)

target_compile_features(QuotaLayoutTests PRIVATE cxx_std_20)
target_include_directories(QuotaLayoutTests PRIVATE src)
add_test(NAME QuotaLayoutTests COMMAND QuotaLayoutTests)

add_executable(TaskbarWindowTests
    tests/TaskbarWindowTests.cpp
    src/taskbar/TaskbarWindow.cpp
    src/taskbar/TaskbarWindow.h
    src/taskbar/TooltipController.cpp
    src/taskbar/TooltipController.h
    src/ui/TaskbarRenderer.cpp
    src/ui/TaskbarRenderer.h
)
target_compile_features(TaskbarWindowTests PRIVATE cxx_std_20)
target_compile_definitions(TaskbarWindowTests PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_include_directories(TaskbarWindowTests PRIVATE src)
target_link_libraries(TaskbarWindowTests PRIVATE advapi32 comctl32 d2d1 d3d11 dcomp dwrite dxgi gdi32 user32)
add_test(NAME TaskbarWindowTests COMMAND TaskbarWindowTests)

add_executable(TaskbarCollisionTests
    tests/TaskbarCollisionTests.cpp
    src/taskbar/TaskbarCollision.h
)
target_compile_features(TaskbarCollisionTests PRIVATE cxx_std_20)
target_include_directories(TaskbarCollisionTests PRIVATE src)
add_test(NAME TaskbarCollisionTests COMMAND TaskbarCollisionTests)

add_executable(TooltipTextTests
    tests/TooltipTextTests.cpp
    src/ui/TaskbarPresentation.cpp
    src/ui/TaskbarPresentation.h
    src/ui/TooltipText.cpp
    src/ui/TooltipText.h
)
target_compile_features(TooltipTextTests PRIVATE cxx_std_20)
target_include_directories(TooltipTextTests PRIVATE src)
add_test(NAME TooltipTextTests COMMAND TooltipTextTests)

add_executable(DataLayerTests
    tests/DataLayerTests.cpp
)
target_compile_features(DataLayerTests PRIVATE cxx_std_20)
target_include_directories(DataLayerTests PRIVATE src)
target_compile_definitions(DataLayerTests PRIVATE CQT_FIXTURE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
target_link_libraries(DataLayerTests PRIVATE data_core)
add_test(NAME DataLayerTests COMMAND DataLayerTests)

add_executable(ProductionLogicTests
    tests/ProductionLogicTests.cpp
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/settings/StartupManager.cpp
    src/settings/StartupManager.h
)
target_compile_features(ProductionLogicTests PRIVATE cxx_std_20)
target_compile_definitions(ProductionLogicTests PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_include_directories(ProductionLogicTests PRIVATE src)
target_link_libraries(ProductionLogicTests PRIVATE data_core advapi32)
add_test(NAME ProductionLogicTests COMMAND ProductionLogicTests)

if(MSVC)
    foreach(target data_core taskbar_core CodexQuotaTaskbar CodexQuotaTaskbarPrototype TaskbarProbe TaskbarInteractionProbe AppLifecycleProbe TaskbarStabilityProbe QuotaLayoutTests TaskbarWindowTests TaskbarCollisionTests TooltipTextTests DataLayerTests ProductionLogicTests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endforeach()
    target_link_options(CodexQuotaTaskbarPrototype PRIVATE /MANIFEST:NO)
    target_link_options(CodexQuotaTaskbar PRIVATE /MANIFEST:NO)
    target_link_options(TaskbarProbe PRIVATE /MANIFEST:NO)
    target_link_options(TaskbarInteractionProbe PRIVATE /MANIFEST:NO)
    target_link_options(AppLifecycleProbe PRIVATE /MANIFEST:NO)
    target_link_options(TaskbarStabilityProbe PRIVATE /MANIFEST:NO)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT CodexQuotaTaskbar)
