cmake_minimum_required(VERSION 3.28)

project(chat-view-obs VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ChatView OBS v0.1 currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

option(CHATVIEW_WARNINGS_AS_ERRORS "Treat ChatView compiler warnings as errors" OFF)
include(CTest)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

set(CHATVIEW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(CHATVIEW_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/rundir/obs-plugins/64bit")

add_library(chat-view-obs MODULE
    src/plugin/plugin-main.cpp
    src/plugin/runtime-controller.cpp
    src/plugin/runtime-controller.hpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
)

target_include_directories(chat-view-obs PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-obs PRIVATE OBS::libobs OBS::obs-frontend-api)
target_compile_definitions(chat-view-obs PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    CHATVIEW_VERSION="${PROJECT_VERSION}"
)
set_target_properties(chat-view-obs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "chat-view-obs"
)

add_executable(chat-view-hud WIN32
    src/hud/main.cpp
    src/hud/hud-placement.cpp
    src/hud/hud-placement.hpp
    src/hud/hud-window.cpp
    src/hud/hud-window.hpp
    src/hud/shared-state-reader.cpp
    src/hud/shared-state-reader.hpp
    src/common/shared-state.hpp
    src/common/win32-handle.hpp
)

target_include_directories(chat-view-hud PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-hud PRIVATE gdiplus shell32 user32 gdi32)
target_compile_definitions(chat-view-hud PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    CHATVIEW_VERSION="${PROJECT_VERSION}"
)
set_target_properties(chat-view-hud PROPERTIES
    OUTPUT_NAME "chat-view-hud"
)

foreach(target chat-view-obs chat-view-hud)
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
endforeach()

if(MSVC)
    foreach(target chat-view-obs chat-view-hud)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(CHATVIEW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(chat-view-hud-placement-test
        tests/hud-placement-test.cpp
        src/hud/hud-placement.cpp
        src/hud/hud-placement.hpp
    )
    target_include_directories(chat-view-hud-placement-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-hud-placement-test PRIVATE user32)
    target_compile_definitions(chat-view-hud-placement-test PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )

    if(MSVC)
        target_compile_options(
            chat-view-hud-placement-test PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(CHATVIEW_WARNINGS_AS_ERRORS)
            target_compile_options(chat-view-hud-placement-test PRIVATE /WX)
        endif()
    endif()

    add_test(NAME chat-view-hud-placement COMMAND chat-view-hud-placement-test)

    add_executable(chat-view-hud-smoke-test
        tests/hud-smoke-test.cpp
        src/common/shared-state.hpp
        src/common/win32-handle.hpp
    )
    target_include_directories(chat-view-hud-smoke-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-hud-smoke-test PRIVATE user32)
    target_compile_definitions(chat-view-hud-smoke-test PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )

    if(MSVC)
        target_compile_options(
            chat-view-hud-smoke-test PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(CHATVIEW_WARNINGS_AS_ERRORS)
            target_compile_options(chat-view-hud-smoke-test PRIVATE /WX)
        endif()
    endif()

    add_test(
        NAME chat-view-hud-smoke
        COMMAND chat-view-hud-smoke-test "$<TARGET_FILE:chat-view-hud>"
    )
    set_tests_properties(chat-view-hud-smoke PROPERTIES TIMEOUT 15)
endif()

install(TARGETS chat-view-obs chat-view-hud
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
)

install(DIRECTORY data/
    DESTINATION "data/obs-plugins/chat-view-obs"
)

install(FILES
    README.md
    LICENSE
    scripts/install.ps1
    scripts/uninstall.ps1
    DESTINATION "."
)
