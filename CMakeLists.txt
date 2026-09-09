cmake_minimum_required(VERSION 3.28)

project(chat-view-obs VERSION 0.2.2 LANGUAGES CXX)

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

add_library(chat-view-obs MODULE
    src/plugin/plugin-main.cpp
    src/plugin/runtime-controller.cpp
    src/plugin/runtime-controller.hpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-obs PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-obs PRIVATE OBS::libobs OBS::obs-frontend-api)
target_compile_definitions(chat-view-obs PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-obs)
set_target_properties(chat-view-obs PROPERTIES PREFIX "" OUTPUT_NAME "chat-view-obs")
chatview_enable_warnings(chat-view-obs)
chatview_set_runtime_output(chat-view-obs)

add_executable(chat-view-hud WIN32
    src/hud/main.cpp
    src/hud/hud-placement.cpp
    src/hud/hud-placement.hpp
    src/hud/hud-window.cpp
    src/hud/hud-window.hpp
    src/hud/shared-state-reader.cpp
    src/hud/shared-state-reader.hpp
    src/hud/webview-host.cpp
    src/hud/webview-host.hpp
    src/common/chat-config.hpp
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
    src/common/window-messages.hpp
)
target_include_directories(chat-view-config PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-config PRIVATE chat-view-common user32 gdi32)
target_compile_definitions(chat-view-config PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-config)
set_target_properties(chat-view-config PROPERTIES OUTPUT_NAME "chat-view-config")
chatview_enable_warnings(chat-view-config)
chatview_set_runtime_output(chat-view-config)

add_executable(chat-view-self-test
    src/preflight/hud-self-test.cpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(chat-view-self-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-self-test PRIVATE user32)
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

    add_test(
        NAME chat-view-hud-smoke
        COMMAND chat-view-self-test "$<TARGET_FILE:chat-view-hud>"
    )
    set_tests_properties(chat-view-hud-smoke PROPERTIES TIMEOUT 30)

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
    chatview_enable_win32(chat-view-obs-runtime-test)
    chatview_enable_warnings(chat-view-obs-runtime-test)
endif()

install(TARGETS chat-view-obs chat-view-hud chat-view-config
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
    DESTINATION "."
)
