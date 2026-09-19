# The renderer is compiled into the binary: no extra network origin, installed
# web server, local URL exception, or developer secret is needed for display.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/chat-renderer.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/native-chat.js")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/chat-renderer.js" CHATVIEW_RENDERER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/native-chat.js" CHATVIEW_NATIVE_BRIDGE)
set(CHATVIEW_NONCE "__CHATVIEW_NATIVE_NONCE__")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hud/native-chat-document.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/native-chat-document.hpp" @ONLY)

target_sources(chat-view-hud PRIVATE src/hud/native-chat-surface.cpp)
target_include_directories(chat-view-hud PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(chat-view-hud PRIVATE bcrypt)

if(BUILD_TESTING)
    add_executable(chat-view-native-chat-surface-test
        tests/native-chat-surface-test.cpp
        src/hud/native-chat-surface.cpp
        src/hud/webview-host.cpp
        src/hud/host-state-message.cpp
        src/hud/page-health-message.cpp)
    target_include_directories(chat-view-native-chat-surface-test PRIVATE
        "${CHATVIEW_SOURCE_DIR}" "${WEBVIEW2_INCLUDE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(chat-view-native-chat-surface-test PRIVATE
        chat-view-common "${WEBVIEW2_LOADER_LIBRARY}"
        bcrypt d3d11 dcomp dxgi ole32 shell32 user32)
    target_compile_definitions(chat-view-native-chat-surface-test PRIVATE WEBVIEW2_STATIC)
    chatview_enable_win32(chat-view-native-chat-surface-test)
    chatview_enable_warnings(chat-view-native-chat-surface-test)
    add_test(NAME chat-view-native-chat-surface COMMAND chat-view-native-chat-surface-test)
    set_tests_properties(chat-view-native-chat-surface PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

    add_executable(chat-view-hud-launch-options-test tests/hud-launch-options-test.cpp)
    target_include_directories(chat-view-hud-launch-options-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-hud-launch-options-test)
    add_test(NAME chat-view-hud-launch-options COMMAND chat-view-hud-launch-options-test)

    add_executable(chat-view-companion-launch-test tests/companion-launch-test.cpp)
    target_include_directories(chat-view-companion-launch-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    target_link_libraries(chat-view-companion-launch-test PRIVATE user32)
    chatview_enable_win32(chat-view-companion-launch-test)
    chatview_enable_warnings(chat-view-companion-launch-test)
    add_test(NAME chat-view-companion-launch
        COMMAND chat-view-companion-launch-test "$<TARGET_FILE:chat-view-hud>")
    set_tests_properties(chat-view-companion-launch PROPERTIES TIMEOUT 70 RUN_SERIAL TRUE)

    # Same test, limits and exercise counts. Full qualification runs it unfiltered.
    set_tests_properties(chat-view-hud-resource-soak PROPERTIES LABELS qualification)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/native-chat-delivery.cmake")
