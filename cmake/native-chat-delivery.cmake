target_sources(chat-view-hud PRIVATE
    src/hud/display-client.cpp src/hud/native-chat-connection.cpp)
target_link_libraries(chat-view-hud PRIVATE winhttp windowsapp gdi32)

if(BUILD_TESTING)
    find_program(CHATVIEW_NODE_EXECUTABLE node REQUIRED)
    add_executable(chat-view-native-display-client-test
        tests/native-display-client-test.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-display-client-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-display-client-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32 gdi32 wtsapi32 winhttp windowsapp version)
    target_compile_definitions(chat-view-native-display-client-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-display-client-test)
    chatview_enable_warnings(chat-view-native-display-client-test)
    add_test(NAME chat-view-native-display-client
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/native-display-test.mts"
        $<TARGET_FILE:chat-view-native-display-client-test>)
    set_tests_properties(chat-view-native-display-client PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()
