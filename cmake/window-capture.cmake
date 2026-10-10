# Official Windows window capture and GPU presentation, no OBS/video encoder in
# the gaming-PC path. The existing companion owns explicit selection/lifecycle.
add_library(chat-view-window-capture STATIC
    src/hud/window-capture.cpp src/hud/window-capture.hpp src/hud/video-layout.hpp
    src/hud/capture-worker-wait.hpp)
target_include_directories(chat-view-window-capture PUBLIC "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-window-capture PUBLIC windowsapp d3d11 d2d1 dxgi dwmapi ole32 user32)
chatview_enable_win32(chat-view-window-capture)
chatview_enable_warnings(chat-view-window-capture)
target_sources(chat-view-hud PRIVATE src/hud/video-output-panel.cpp src/hud/video-output-panel.hpp
    src/hud/video-output-check.hpp src/hud/video-output-pattern.hpp)
target_link_libraries(chat-view-hud PRIVATE chat-view-window-capture)

if(BUILD_TESTING)
    # CI-only libobs ownership regression. This dependency is not linked into
    # the companion, its window-capture library or any gaming-PC executable.
    add_executable(chat-view-obs-capture-scene-test
        tests/obs-capture-scene-test.cpp tests/obs-capture-scene.hpp)
    target_link_libraries(chat-view-obs-capture-scene-test PRIVATE OBS::libobs)
    chatview_enable_win32(chat-view-obs-capture-scene-test)
    chatview_enable_warnings(chat-view-obs-capture-scene-test)
    add_test(NAME chat-view-obs-capture-scene COMMAND chat-view-obs-capture-scene-test)
    set_tests_properties(chat-view-obs-capture-scene PROPERTIES TIMEOUT 10
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:OBS::libobs>")
    if(OBS_RUNTIME_DEPENDENCY_DIR)
        set_property(TEST chat-view-obs-capture-scene APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${OBS_RUNTIME_DEPENDENCY_DIR}/bin")
    endif()

    add_executable(chat-view-video-layout-test tests/video-layout-test.cpp)
    target_include_directories(chat-view-video-layout-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-video-layout-test)
    add_test(NAME chat-view-video-layout COMMAND chat-view-video-layout-test)

    add_executable(chat-view-video-output-check-test tests/video-output-check-test.cpp)
    target_include_directories(chat-view-video-output-check-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-video-output-check-test)
    add_test(NAME chat-view-video-output-check COMMAND chat-view-video-output-check-test)

    add_executable(chat-view-window-capture-test tests/window-capture-test.cpp)
    target_link_libraries(chat-view-window-capture-test PRIVATE chat-view-window-capture gdi32)
    chatview_enable_win32(chat-view-window-capture-test)
    chatview_enable_warnings(chat-view-window-capture-test)
    add_test(NAME chat-view-window-capture COMMAND chat-view-window-capture-test)
    set_tests_properties(chat-view-window-capture PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)

    add_executable(chat-view-capture-worker-wait-test tests/capture-worker-wait-test.cpp)
    target_include_directories(chat-view-capture-worker-wait-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-capture-worker-wait-test PRIVATE user32)
    chatview_enable_win32(chat-view-capture-worker-wait-test)
    chatview_enable_warnings(chat-view-capture-worker-wait-test)
    add_test(NAME chat-view-capture-worker-wait COMMAND chat-view-capture-worker-wait-test)
    set_tests_properties(chat-view-capture-worker-wait PROPERTIES TIMEOUT 10)

    add_executable(chat-view-video-output-lifecycle-test
        tests/video-output-lifecycle-test.cpp src/hud/video-output-panel.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp src/hud/webview-host.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-video-output-lifecycle-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(chat-view-video-output-lifecycle-test PRIVATE
        chat-view-common chat-view-window-capture "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt dcomp gdi32 shell32 wtsapi32 version)
    target_compile_definitions(chat-view-video-output-lifecycle-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-video-output-lifecycle-test)
    chatview_enable_warnings(chat-view-video-output-lifecycle-test)
    add_test(NAME chat-view-video-output-lifecycle COMMAND chat-view-video-output-lifecycle-test
        "$<TARGET_FILE:chat-view-window-capture-test>")
    set_tests_properties(chat-view-video-output-lifecycle PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

    add_executable(chat-view-video-target-loss-test
        tests/video-target-loss-test.cpp src/hud/video-output-panel.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp src/hud/webview-host.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-video-target-loss-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(chat-view-video-target-loss-test PRIVATE
        chat-view-common chat-view-window-capture "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt dcomp gdi32 shell32 wtsapi32 version)
    target_compile_definitions(chat-view-video-target-loss-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-video-target-loss-test)
    chatview_enable_warnings(chat-view-video-target-loss-test)
    add_test(NAME chat-view-video-target-loss COMMAND chat-view-video-target-loss-test
        "$<TARGET_FILE:chat-view-window-capture-test>")
    set_tests_properties(chat-view-video-target-loss PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

    # Both production companion panels share the actual HUD and WGC worker.
    # Only receiver topology/consent is synthetic; no libobs in this executable.
    add_executable(chat-view-companion-video-protection-test
        tests/companion-video-protection-test.cpp src/hud/video-output-panel.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-companion-video-protection-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-companion-video-protection-test PRIVATE
        chat-view-common chat-view-window-capture "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt dcomp gdi32 shell32 wtsapi32 winhttp version crypt32 comctl32)
    target_compile_definitions(chat-view-companion-video-protection-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-companion-video-protection-test)
    chatview_enable_warnings(chat-view-companion-video-protection-test)
    add_test(NAME chat-view-companion-video-protection COMMAND chat-view-companion-video-protection-test
        "$<TARGET_FILE:chat-view-window-capture-test>")
    set_tests_properties(chat-view-companion-video-protection PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
