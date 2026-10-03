# Official Windows window capture and GPU presentation, no OBS/video encoder in
# the gaming-PC path. The existing companion owns explicit selection/lifecycle.
add_library(chat-view-window-capture STATIC
    src/hud/window-capture.cpp src/hud/window-capture.hpp src/hud/video-layout.hpp
    src/hud/capture-worker-wait.hpp)
target_include_directories(chat-view-window-capture PUBLIC "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(chat-view-window-capture PUBLIC windowsapp d3d11 d2d1 dxgi dwmapi ole32 user32)
chatview_enable_win32(chat-view-window-capture)
chatview_enable_warnings(chat-view-window-capture)
target_sources(chat-view-hud PRIVATE src/hud/video-output-panel.cpp src/hud/video-output-panel.hpp)
target_link_libraries(chat-view-hud PRIVATE chat-view-window-capture)

if(BUILD_TESTING)
    add_executable(chat-view-video-layout-test tests/video-layout-test.cpp)
    target_include_directories(chat-view-video-layout-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-video-layout-test)
    add_test(NAME chat-view-video-layout COMMAND chat-view-video-layout-test)

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
endif()
