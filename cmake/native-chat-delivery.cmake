target_sources(chat-view-hud PRIVATE
    src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp)
target_link_libraries(chat-view-hud PRIVATE winhttp windowsapp gdi32 crypt32 comctl32)

if(BUILD_TESTING)
    add_executable(chat-view-native-chat-control-test tests/native-chat-control-test.cpp)
    target_include_directories(chat-view-native-chat-control-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-native-chat-control-test)
    add_test(NAME chat-view-native-chat-control COMMAND chat-view-native-chat-control-test)

    find_program(CHATVIEW_NODE_EXECUTABLE node REQUIRED)
    add_executable(chat-view-native-display-client-test
        tests/native-display-client-test.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-display-client-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-display-client-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32 gdi32 wtsapi32 winhttp windowsapp version crypt32 comctl32)
    target_compile_definitions(chat-view-native-display-client-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-display-client-test)
    chatview_enable_warnings(chat-view-native-display-client-test)
    add_test(NAME chat-view-native-display-client
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/native-display-test.mts"
        $<TARGET_FILE:chat-view-native-display-client-test>)
    set_tests_properties(chat-view-native-display-client PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

    add_executable(chat-view-session-display-client-test
        tests/session-display-client-test.cpp src/hud/saved-connection.cpp src/hud/display-client.cpp)
    target_include_directories(chat-view-session-display-client-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-session-display-client-test PRIVATE winhttp windowsapp crypt32 bcrypt)
    chatview_enable_win32(chat-view-session-display-client-test)
    chatview_enable_warnings(chat-view-session-display-client-test)
    add_test(NAME chat-view-session-display-client
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/session-display-test.mts"
        $<TARGET_FILE:chat-view-session-display-client-test>)
    set_tests_properties(chat-view-session-display-client PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

    add_executable(chat-view-native-logout-test
        tests/native-logout-test.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-logout-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-logout-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32 gdi32 wtsapi32 winhttp windowsapp version crypt32 comctl32)
    target_compile_definitions(chat-view-native-logout-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-logout-test)
    chatview_enable_warnings(chat-view-native-logout-test)
    add_test(NAME chat-view-native-logout
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/native-logout-test.mts"
        $<TARGET_FILE:chat-view-native-logout-test>)
    set_tests_properties(chat-view-native-logout PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

    add_executable(chat-view-native-chat-switch-test
        tests/native-chat-switch-test.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-chat-switch-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-chat-switch-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32 gdi32 wtsapi32 winhttp windowsapp version crypt32 comctl32)
    target_compile_definitions(chat-view-native-chat-switch-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-chat-switch-test)
    chatview_enable_warnings(chat-view-native-chat-switch-test)
    add_test(NAME chat-view-native-chat-switch
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/native-chat-switch-test.mts"
        $<TARGET_FILE:chat-view-native-chat-switch-test>)
    set_tests_properties(chat-view-native-chat-switch PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

    add_executable(chat-view-native-output-test
        tests/native-output-test.cpp src/hud/shared-state-reader.cpp
        src/hud/display-client.cpp src/hud/native-chat-connection.cpp src/hud/saved-connection.cpp
        src/hud/native-chat-surface.cpp src/hud/webview-host.cpp
        src/hud/hud-window.cpp src/hud/hud-placement.cpp
        src/hud/host-state-message.cpp src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-output-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-output-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32 gdi32 wtsapi32 winhttp windowsapp version crypt32 comctl32)
    target_compile_definitions(chat-view-native-output-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-output-test)
    chatview_enable_warnings(chat-view-native-output-test)
    add_test(NAME chat-view-native-output
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/native-output-test.mts"
        $<TARGET_FILE:chat-view-native-output-test>)
    set_tests_properties(chat-view-native-output PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

    add_executable(chat-view-obs-output-test tests/obs-output-test.cpp)
    target_include_directories(chat-view-obs-output-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-obs-output-test)
    add_test(NAME chat-view-obs-output COMMAND chat-view-obs-output-test)
    add_executable(chat-view-public-ad-browser-test tests/public-ad-browser-test.cpp)
    target_include_directories(chat-view-public-ad-browser-test PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(chat-view-public-ad-browser-test PRIVATE "${WEBVIEW2_LOADER_LIBRARY}" ole32 user32)
    target_compile_definitions(chat-view-public-ad-browser-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-public-ad-browser-test)
    chatview_enable_warnings(chat-view-public-ad-browser-test)
    add_test(NAME chat-view-public-ad-browser
        COMMAND "${CHATVIEW_NODE_EXECUTABLE}" --experimental-strip-types
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/tools/public-ad-browser-test.mts"
        $<TARGET_FILE:chat-view-public-ad-browser-test>)
    set_tests_properties(chat-view-public-ad-browser PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
endif()
