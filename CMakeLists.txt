cmake_minimum_required(VERSION 3.28)

project(chat-view-obs VERSION 0.3.9 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ChatView OBS currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CHATVIEW_WARNINGS_AS_ERRORS "Treat ChatView compiler warnings as errors" OFF)
set(WEBVIEW2_SDK_DIR "" CACHE PATH "Extracted Microsoft.Web.WebView2 NuGet package")
set(OBS_RUNTIME_DEPENDENCY_DIR "" CACHE PATH "OBS dependency prefix used by loader tests")
include(CTest)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

set(CHATVIEW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(CHATVIEW_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/rundir/obs-plugins/64bit")
set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_DIR}/build/native/include")
set(WEBVIEW2_LOADER_LIBRARY "${WEBVIEW2_SDK_DIR}/build/native/x64/WebView2LoaderStatic.lib")

if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(FATAL_ERROR "WebView2.h was not found. Set WEBVIEW2_SDK_DIR to the extracted Microsoft.Web.WebView2 package.")
endif()
if(NOT EXISTS "${WEBVIEW2_LOADER_LIBRARY}")
    message(FATAL_ERROR "WebView2LoaderStatic.lib was not found for x64.")
endif()

function(chatview_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(CHATVIEW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

function(chatview_set_runtime_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CHATVIEW_RUNTIME_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CHATVIEW_RUNTIME_OUTPUT_DIR}"
    )
    foreach(configuration Debug RelWithDebInfo Release MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${CHATVIEW_RUNTIME_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${CHATVIEW_RUNTIME_OUTPUT_DIR}"
        )
    endforeach()
endfunction()

function(chatview_enable_win32 target)
    target_compile_definitions(${target} PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endfunction()

add_library(chat-view-common STATIC
    src/common/chat-config.cpp
    src/common/chat-config.hpp
)
target_include_directories(chat-view-common PUBLIC "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-common PUBLIC winhttp)
chatview_enable_win32(chat-view-common)
chatview_enable_warnings(chat-view-common)

add_library(chat-view-diagnostic-redaction STATIC
    src/common/diagnostic-redaction.cpp
    src/common/diagnostic-redaction.hpp
)
target_include_directories(
    chat-view-diagnostic-redaction PUBLIC "${CHATVIEW_SOURCE_DIR}")
chatview_enable_win32(chat-view-diagnostic-redaction)
chatview_enable_warnings(chat-view-diagnostic-redaction)

add_library(chat-view-runtime-history STATIC
    src/common/runtime-history-store.cpp
    src/common/runtime-history-store.hpp
    src/common/runtime-telemetry.hpp
)
target_include_directories(
    chat-view-runtime-history PUBLIC "${CHATVIEW_SOURCE_DIR}")
chatview_enable_win32(chat-view-runtime-history)
chatview_enable_warnings(chat-view-runtime-history)

add_library(chat-view-obs MODULE
    src/plugin/plugin-main.cpp
    src/plugin/capture-risk-policy.hpp
    src/plugin/control-center-bridge.cpp
    src/plugin/control-center-bridge.hpp
    src/plugin/runtime-controller.cpp
    src/plugin/runtime-controller.hpp
    src/plugin/restart-policy.hpp
    src/plugin/transport-token.cpp
    src/plugin/transport-token.hpp
    src/common/control-status.hpp
    src/common/shared-state.hpp
    src/common/srw-lock.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-obs PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(
    chat-view-obs PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    chat-view-runtime-history
    bcrypt
)
target_compile_definitions(chat-view-obs PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-obs)
set_target_properties(chat-view-obs PROPERTIES PREFIX "" OUTPUT_NAME "chat-view-obs")
chatview_enable_warnings(chat-view-obs)
chatview_set_runtime_output(chat-view-obs)

add_executable(chat-view-hud WIN32
    src/hud/main.cpp
    src/hud/host-state-message.cpp
    src/hud/host-state-message.hpp
    src/hud/page-health-message.cpp
    src/hud/page-health-message.hpp
    src/hud/page-connection-recovery.hpp
    src/hud/page-health-watchdog.hpp
    src/hud/system-lifecycle.hpp
    src/hud/hud-placement.cpp
    src/hud/hud-placement.hpp
    src/hud/hud-window.cpp
    src/hud/hud-window.hpp
    src/hud/shared-state-reader.cpp
    src/hud/shared-state-reader.hpp
    src/hud/webview-host.cpp
    src/hud/webview-host.hpp
    src/common/chat-config.hpp
    src/common/hud-health.hpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-hud PRIVATE
    "${CHATVIEW_SOURCE_DIR}"
    "${WEBVIEW2_INCLUDE_DIR}"
)
target_link_libraries(chat-view-hud PRIVATE
    chat-view-common
    "${WEBVIEW2_LOADER_LIBRARY}"
    d3d11
    dcomp
    dxgi
    ole32
    shell32
    user32
    version
    wtsapi32
)
target_compile_definitions(chat-view-hud PRIVATE
    WEBVIEW2_STATIC
    CHATVIEW_VERSION="${PROJECT_VERSION}"
)
chatview_enable_win32(chat-view-hud)
set_target_properties(chat-view-hud PROPERTIES OUTPUT_NAME "chat-view-hud")
chatview_enable_warnings(chat-view-hud)
chatview_set_runtime_output(chat-view-hud)

add_executable(chat-view-config WIN32
    src/config/main.cpp
    src/config/control-status-reader.cpp
    src/config/control-status-reader.hpp
    src/common/control-status.hpp
    src/common/hud-health.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-config PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(
    chat-view-config PRIVATE chat-view-common user32 gdi32 shell32)
target_compile_definitions(chat-view-config PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-config)
set_target_properties(chat-view-config PROPERTIES OUTPUT_NAME "chat-view-config")
chatview_enable_warnings(chat-view-config)
chatview_set_runtime_output(chat-view-config)

add_library(chat-view-diagnostics-core STATIC
    src/diagnostics/diagnostics-exporter.cpp
    src/diagnostics/diagnostics-exporter.hpp
)
target_include_directories(
    chat-view-diagnostics-core PUBLIC "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-diagnostics-core PUBLIC
    chat-view-common
    chat-view-diagnostic-redaction
    chat-view-runtime-history
    bcrypt
    ole32
    shell32
    version
)
target_compile_definitions(
    chat-view-diagnostics-core PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-diagnostics-core)
chatview_enable_warnings(chat-view-diagnostics-core)
target_link_libraries(
    chat-view-config PRIVATE chat-view-diagnostics-core)

add_executable(chat-view-diagnostics WIN32
    src/diagnostics/main.cpp
)
target_link_libraries(
    chat-view-diagnostics PRIVATE chat-view-diagnostics-core shell32 user32)
chatview_enable_win32(chat-view-diagnostics)
set_target_properties(
    chat-view-diagnostics PROPERTIES OUTPUT_NAME "chat-view-diagnostics")
chatview_enable_warnings(chat-view-diagnostics)
chatview_set_runtime_output(chat-view-diagnostics)

add_executable(chat-view-self-test
    src/preflight/hud-self-test.cpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-self-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-self-test PRIVATE dwmapi gdi32 user32)
chatview_enable_win32(chat-view-self-test)
set_target_properties(chat-view-self-test PROPERTIES OUTPUT_NAME "chat-view-self-test")
chatview_enable_warnings(chat-view-self-test)

add_executable(chat-view-plugin-preflight
    src/preflight/plugin-load-test.cpp
)
chatview_enable_win32(chat-view-plugin-preflight)
set_target_properties(
    chat-view-plugin-preflight PROPERTIES OUTPUT_NAME "chat-view-plugin-preflight")
chatview_enable_warnings(chat-view-plugin-preflight)

if(BUILD_TESTING)
    add_executable(chat-view-chat-config-test
        tests/chat-config-test.cpp
    )
    target_link_libraries(chat-view-chat-config-test PRIVATE chat-view-common)
    chatview_enable_warnings(chat-view-chat-config-test)
    add_test(NAME chat-view-chat-config COMMAND chat-view-chat-config-test)

    add_executable(chat-view-diagnostic-redaction-test
        tests/diagnostic-redaction-test.cpp
    )
    target_link_libraries(
        chat-view-diagnostic-redaction-test PRIVATE
        chat-view-diagnostic-redaction)
    chatview_enable_warnings(chat-view-diagnostic-redaction-test)
    add_test(
        NAME chat-view-diagnostic-redaction
        COMMAND chat-view-diagnostic-redaction-test
    )

    add_executable(chat-view-runtime-telemetry-test
        tests/runtime-telemetry-test.cpp
    )
    target_include_directories(
        chat-view-runtime-telemetry-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-runtime-telemetry-test)
    add_test(
        NAME chat-view-runtime-telemetry
        COMMAND chat-view-runtime-telemetry-test
    )

    add_executable(chat-view-runtime-history-store-test
        tests/runtime-history-store-test.cpp
    )
    target_link_libraries(
        chat-view-runtime-history-store-test PRIVATE
        chat-view-runtime-history)
    chatview_enable_win32(chat-view-runtime-history-store-test)
    chatview_enable_warnings(chat-view-runtime-history-store-test)
    add_test(
        NAME chat-view-runtime-history-store
        COMMAND chat-view-runtime-history-store-test
    )

    add_executable(chat-view-diagnostics-exporter-test
        tests/diagnostics-exporter-test.cpp
    )
    target_link_libraries(
        chat-view-diagnostics-exporter-test PRIVATE
        chat-view-diagnostics-core)
    chatview_enable_win32(chat-view-diagnostics-exporter-test)
    chatview_enable_warnings(chat-view-diagnostics-exporter-test)
    add_test(
        NAME chat-view-diagnostics-exporter
        COMMAND chat-view-diagnostics-exporter-test
    )

    add_executable(chat-view-control-status-reader-test
        tests/control-status-reader-test.cpp
        src/config/control-status-reader.cpp
        src/config/control-status-reader.hpp
        src/common/control-status.hpp
        src/common/win32-handle.hpp
    )
    target_include_directories(
        chat-view-control-status-reader-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-control-status-reader-test)
    chatview_enable_warnings(chat-view-control-status-reader-test)
    add_test(
        NAME chat-view-control-status-reader
        COMMAND chat-view-control-status-reader-test
    )

    add_executable(chat-view-config-single-instance-test
        tests/config-single-instance-test.cpp
        src/common/hud-health.hpp
        src/common/win32-handle.hpp
        src/common/window-messages.hpp
    )
    target_include_directories(
        chat-view-config-single-instance-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-config-single-instance-test PRIVATE user32)
    chatview_enable_win32(chat-view-config-single-instance-test)
    chatview_enable_warnings(chat-view-config-single-instance-test)
    add_test(
        NAME chat-view-config-single-instance
        COMMAND chat-view-config-single-instance-test "$<TARGET_FILE:chat-view-config>"
    )
    set_tests_properties(chat-view-config-single-instance PROPERTIES TIMEOUT 15)

    add_executable(chat-view-hud-health-test
        tests/hud-health-test.cpp
        src/common/hud-health.hpp
    )
    target_include_directories(
        chat-view-hud-health-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-hud-health-test)
    add_test(
        NAME chat-view-hud-health
        COMMAND chat-view-hud-health-test
    )

    add_executable(chat-view-page-health-message-test
        tests/page-health-message-test.cpp
        src/hud/page-health-message.cpp
        src/hud/page-health-message.hpp
        src/common/hud-health.hpp
    )
    target_include_directories(
        chat-view-page-health-message-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-page-health-message-test)
    add_test(
        NAME chat-view-page-health-message
        COMMAND chat-view-page-health-message-test
    )

    add_executable(chat-view-page-health-watchdog-test
        tests/page-health-watchdog-test.cpp
        src/hud/page-health-watchdog.hpp
    )
    target_include_directories(
        chat-view-page-health-watchdog-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-page-health-watchdog-test)
    add_test(
        NAME chat-view-page-health-watchdog
        COMMAND chat-view-page-health-watchdog-test
    )

    add_executable(chat-view-system-lifecycle-test
        tests/system-lifecycle-test.cpp
        src/hud/system-lifecycle.hpp
    )
    target_include_directories(
        chat-view-system-lifecycle-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-system-lifecycle-test)
    add_test(
        NAME chat-view-system-lifecycle
        COMMAND chat-view-system-lifecycle-test
    )

    add_executable(chat-view-page-connection-recovery-test
        tests/page-connection-recovery-test.cpp
        src/hud/page-connection-recovery.hpp
    )
    target_include_directories(
        chat-view-page-connection-recovery-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-page-connection-recovery-test)
    add_test(
        NAME chat-view-page-connection-recovery
        COMMAND chat-view-page-connection-recovery-test
    )

    add_executable(chat-view-hud-placement-test
        tests/hud-placement-test.cpp
        src/hud/hud-placement.cpp
        src/hud/hud-placement.hpp
    )
    target_include_directories(chat-view-hud-placement-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-hud-placement-test PRIVATE user32)
    chatview_enable_win32(chat-view-hud-placement-test)
    chatview_enable_warnings(chat-view-hud-placement-test)
    add_test(NAME chat-view-hud-placement COMMAND chat-view-hud-placement-test)

    add_executable(chat-view-shared-state-reader-test
        tests/shared-state-reader-test.cpp
        src/hud/shared-state-reader.cpp
        src/hud/shared-state-reader.hpp
        src/common/shared-state.hpp
        src/common/win32-handle.hpp
    )
    target_include_directories(
        chat-view-shared-state-reader-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_win32(chat-view-shared-state-reader-test)
    chatview_enable_warnings(chat-view-shared-state-reader-test)
    add_test(
        NAME chat-view-shared-state-reader
        COMMAND chat-view-shared-state-reader-test
    )

    add_executable(chat-view-transport-token-test
        tests/transport-token-test.cpp
        src/plugin/transport-token.cpp
        src/plugin/transport-token.hpp
    )
    target_include_directories(
        chat-view-transport-token-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-transport-token-test PRIVATE bcrypt)
    chatview_enable_win32(chat-view-transport-token-test)
    chatview_enable_warnings(chat-view-transport-token-test)
    add_test(
        NAME chat-view-transport-token
        COMMAND chat-view-transport-token-test
    )

    add_executable(chat-view-restart-policy-test
        tests/restart-policy-test.cpp
        src/plugin/restart-policy.hpp
    )
    target_include_directories(
        chat-view-restart-policy-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-restart-policy-test)
    add_test(
        NAME chat-view-restart-policy
        COMMAND chat-view-restart-policy-test
    )

    add_executable(chat-view-capture-risk-policy-test
        tests/capture-risk-policy-test.cpp
        src/plugin/capture-risk-policy.hpp
    )
    target_include_directories(
        chat-view-capture-risk-policy-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-capture-risk-policy-test)
    add_test(
        NAME chat-view-capture-risk-policy
        COMMAND chat-view-capture-risk-policy-test
    )

    add_executable(chat-view-host-state-message-test
        tests/host-state-message-test.cpp
        src/hud/host-state-message.cpp
        src/hud/host-state-message.hpp
    )
    target_include_directories(
        chat-view-host-state-message-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-host-state-message-test)
    add_test(
        NAME chat-view-host-state-message
        COMMAND chat-view-host-state-message-test
    )

    add_test(
        NAME chat-view-hud-smoke
        COMMAND chat-view-self-test "$<TARGET_FILE:chat-view-hud>"
    )
    set_tests_properties(chat-view-hud-smoke PROPERTIES TIMEOUT 30)

    add_executable(chat-view-hud-resource-soak-test
        tests/hud-resource-soak-test.cpp
        src/common/shared-state.hpp
        src/common/win32-handle.hpp
        src/common/window-messages.hpp
    )
    target_include_directories(
        chat-view-hud-resource-soak-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(
        chat-view-hud-resource-soak-test PRIVATE psapi user32)
    chatview_enable_win32(chat-view-hud-resource-soak-test)
    chatview_enable_warnings(chat-view-hud-resource-soak-test)
    add_test(
        NAME chat-view-hud-resource-soak
        COMMAND chat-view-hud-resource-soak-test "$<TARGET_FILE:chat-view-hud>"
    )
    set_tests_properties(
        chat-view-hud-resource-soak PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

    set(obs_load_test_arguments
        "$<TARGET_FILE:chat-view-obs>"
        "$<TARGET_FILE_DIR:OBS::libobs>"
        "$<TARGET_FILE_DIR:OBS::obs-frontend-api>"
    )
    if(OBS_RUNTIME_DEPENDENCY_DIR)
        list(APPEND obs_load_test_arguments "${OBS_RUNTIME_DEPENDENCY_DIR}/bin")
    endif()

    add_test(
        NAME chat-view-obs-load
        COMMAND chat-view-plugin-preflight ${obs_load_test_arguments}
    )
    set_tests_properties(chat-view-obs-load PROPERTIES TIMEOUT 10)

    add_executable(chat-view-obs-runtime-test
        tests/obs-runtime-smoke-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-obs-runtime-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(
        chat-view-obs-runtime-test PRIVATE chat-view-runtime-history)
    chatview_enable_win32(chat-view-obs-runtime-test)
    chatview_enable_warnings(chat-view-obs-runtime-test)

    add_library(chat-view-obs-capture-qualification MODULE
        tests/obs-capture-qualification-plugin.cpp
    )
    target_link_libraries(
        chat-view-obs-capture-qualification PRIVATE
        OBS::libobs
        OBS::obs-frontend-api
        dwmapi
        user32
    )
    chatview_enable_win32(chat-view-obs-capture-qualification)
    set_target_properties(
        chat-view-obs-capture-qualification PROPERTIES
        PREFIX ""
        OUTPUT_NAME "chat-view-obs-capture-qualification"
    )
    chatview_enable_warnings(chat-view-obs-capture-qualification)
    chatview_set_runtime_output(chat-view-obs-capture-qualification)

    add_executable(chat-view-obs-capture-qualification-test
        tests/obs-capture-qualification-test.cpp
        src/common/win32-handle.hpp
    )
    target_include_directories(
        chat-view-obs-capture-qualification-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}"
    )
    target_link_libraries(
        chat-view-obs-capture-qualification-test PRIVATE user32)
    chatview_enable_win32(chat-view-obs-capture-qualification-test)
    chatview_enable_warnings(chat-view-obs-capture-qualification-test)
endif()

install(TARGETS
    chat-view-obs
    chat-view-hud
    chat-view-config
    chat-view-diagnostics
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
)
install(TARGETS chat-view-self-test chat-view-plugin-preflight
    RUNTIME DESTINATION "."
)
install(DIRECTORY data/ DESTINATION "data/obs-plugins/chat-view-obs")
install(FILES
    README.md
    LICENSE
    scripts/install.cmd
    scripts/install.ps1
    scripts/uninstall.cmd
    scripts/uninstall.ps1
    scripts/ensure-webview2-runtime.ps1
    scripts/verify-package.ps1
    DESTINATION "."
)
