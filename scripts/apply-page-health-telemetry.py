from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[1]

# CMake integration and version.
path = root / "CMakeLists.txt"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "project(chat-view-obs VERSION 0.3.2 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.3.3 LANGUAGES CXX)",
    "project version",
)
text = replace_once(
    text,
    """    src/hud/host-state-message.cpp\n    src/hud/host-state-message.hpp\n    src/hud/hud-placement.cpp\n""",
    """    src/hud/host-state-message.cpp\n    src/hud/host-state-message.hpp\n    src/hud/page-health-message.cpp\n    src/hud/page-health-message.hpp\n    src/hud/hud-placement.cpp\n""",
    "HUD page-health sources",
)
text = replace_once(
    text,
    """    add_test(\n        NAME chat-view-hud-health\n        COMMAND chat-view-hud-health-test\n    )\n\n    add_executable(chat-view-hud-placement-test\n""",
    """    add_test(\n        NAME chat-view-hud-health\n        COMMAND chat-view-hud-health-test\n    )\n\n    add_executable(chat-view-page-health-message-test\n        tests/page-health-message-test.cpp\n        src/hud/page-health-message.cpp\n        src/hud/page-health-message.hpp\n        src/common/hud-health.hpp\n    )\n    target_include_directories(\n        chat-view-page-health-message-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n    chatview_enable_warnings(chat-view-page-health-message-test)\n    add_test(\n        NAME chat-view-page-health-message\n        COMMAND chat-view-page-health-message-test\n    )\n\n    add_executable(chat-view-hud-placement-test\n""",
    "page-health parser test target",
)
path.write_text(text, encoding="utf-8")

# WebView host declaration.
path = root / "src" / "hud" / "webview-host.hpp"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "inline constexpr UINT kWebViewNavigationFailedMessage = WM_APP + 44U;\n",
    """inline constexpr UINT kWebViewNavigationFailedMessage = WM_APP + 44U;\ninline constexpr UINT kWebViewPageHealthMessage = WM_APP + 45U;\n""",
    "page-health window message",
)
text = replace_once(
    text,
    """    HRESULT on_bootstrap_registered(HRESULT result) noexcept;\n    [[nodiscard]] HRESULT finish_controller_initialization() noexcept;\n""",
    """    HRESULT on_overlay_bootstrap_registered(HRESULT result) noexcept;\n    HRESULT on_page_health_bootstrap_registered(HRESULT result) noexcept;\n    [[nodiscard]] HRESULT finish_controller_initialization() noexcept;\n    void handle_page_health_message(\n        ICoreWebView2WebMessageReceivedEventArgs *args) noexcept;\n""",
    "WebView bootstrap declarations",
)
text = replace_once(
    text,
    """    EventRegistrationToken process_failed_token_{};\n    EventRegistrationToken download_starting_token_{};\n""",
    """    EventRegistrationToken process_failed_token_{};\n    EventRegistrationToken web_message_received_token_{};\n    EventRegistrationToken download_starting_token_{};\n""",
    "WebView message token",
)
path.write_text(text, encoding="utf-8")

# WebView host implementation.
path = root / "src" / "hud" / "webview-host.cpp"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "#include \"hud/host-state-message.hpp\"\n",
    """#include \"hud/host-state-message.hpp\"\n#include \"hud/page-health-message.hpp\"\n""",
    "page-health include",
)
text = replace_once(
    text,
    """        if (process_failed_token_.value != 0) {\n            webview_->remove_ProcessFailed(process_failed_token_);\n        }\n    }\n\n    navigation_starting_token_ = {};\n""",
    """        if (process_failed_token_.value != 0) {\n            webview_->remove_ProcessFailed(process_failed_token_);\n        }\n        if (web_message_received_token_.value != 0) {\n            webview_->remove_WebMessageReceived(\n                web_message_received_token_);\n        }\n    }\n\n    navigation_starting_token_ = {};\n""",
    "remove page-health handler",
)
text = replace_once(
    text,
    """    process_failed_token_ = {};\n    download_starting_token_ = {};\n""",
    """    process_failed_token_ = {};\n    web_message_received_token_ = {};\n    download_starting_token_ = {};\n""",
    "reset page-health token",
)
text = replace_once(
    text,
    """        &process_failed_token_);\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    result = webview_->AddScriptToExecuteOnDocumentCreated(\n        kOverlayBootstrapScript,\n""",
    """        &process_failed_token_);\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    result = webview_->add_WebMessageReceived(\n        Callback<ICoreWebView2WebMessageReceivedEventHandler>(\n            [state](\n                ICoreWebView2 *,\n                ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {\n                if (state->owner != nullptr && args != nullptr) {\n                    state->owner->handle_page_health_message(args);\n                }\n                return S_OK;\n            })\n            .Get(),\n        &web_message_received_token_);\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    result = webview_->AddScriptToExecuteOnDocumentCreated(\n        kOverlayBootstrapScript,\n""",
    "register page-health message handler",
)
text = replace_once(
    text,
    """                           ? state->owner->on_bootstrap_registered(\n                                 callback_result)\n""",
    """                           ? state->owner->on_overlay_bootstrap_registered(\n                                 callback_result)\n""",
    "overlay bootstrap callback",
)
text = replace_once(
    text,
    """HRESULT WebViewHost::on_bootstrap_registered(HRESULT result) noexcept\n{\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    result = finish_controller_initialization();\n    if (FAILED(result)) {\n        post_failure(result);\n    }\n    return S_OK;\n}\n""",
    """HRESULT WebViewHost::on_overlay_bootstrap_registered(\n    HRESULT result) noexcept\n{\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    const std::shared_ptr<CallbackState> state = callback_state_;\n    result = webview_->AddScriptToExecuteOnDocumentCreated(\n        page_health_bootstrap_script(),\n        Callback<\n            ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(\n            [state](HRESULT callback_result, LPCWSTR) -> HRESULT {\n                return state->owner != nullptr\n                           ? state->owner->on_page_health_bootstrap_registered(\n                                 callback_result)\n                           : S_OK;\n            })\n            .Get());\n    if (FAILED(result)) {\n        post_failure(result);\n    }\n    return S_OK;\n}\n\nHRESULT WebViewHost::on_page_health_bootstrap_registered(\n    HRESULT result) noexcept\n{\n    if (FAILED(result)) {\n        post_failure(result);\n        return S_OK;\n    }\n\n    result = finish_controller_initialization();\n    if (FAILED(result)) {\n        post_failure(result);\n    }\n    return S_OK;\n}\n""",
    "split WebView bootstrap registration",
)
text = replace_once(
    text,
    """void WebViewHost::post_failure(HRESULT result) const noexcept\n""",
    """void WebViewHost::handle_page_health_message(\n    ICoreWebView2WebMessageReceivedEventArgs *args) noexcept\n{\n    if (args == nullptr || window_ == nullptr || current_url_.empty()) {\n        return;\n    }\n\n    LPWSTR source = nullptr;\n    if (FAILED(args->get_Source(&source)) || source == nullptr) {\n        CoTaskMemFree(source);\n        return;\n    }\n    const bool source_allowed = is_navigation_allowed(source);\n    CoTaskMemFree(source);\n    if (!source_allowed) {\n        return;\n    }\n\n    LPWSTR message = nullptr;\n    if (FAILED(args->TryGetWebMessageAsString(&message)) ||\n        message == nullptr) {\n        CoTaskMemFree(message);\n        return;\n    }\n\n    HudHealthSnapshot snapshot;\n    const bool parsed = parse_page_health_message(message, snapshot);\n    CoTaskMemFree(message);\n    if (!parsed ||\n        snapshot.provider != provider_for_chat_document(current_url_)) {\n        return;\n    }\n\n    if (!PostMessageW(\n            window_,\n            kWebViewPageHealthMessage,\n            static_cast<WPARAM>(encode_hud_health(snapshot)),\n            0L)) {\n        post_failure(HRESULT_FROM_WIN32(GetLastError()));\n    }\n}\n\nvoid WebViewHost::post_failure(HRESULT result) const noexcept\n""",
    "page-health message handling",
)
path.write_text(text, encoding="utf-8")

# HUD declaration and state transition handling.
path = root / "src" / "hud" / "hud-window.hpp"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    """    void reload_chat_config() noexcept;\n    void handle_webview_process_failure(\n""",
    """    void reload_chat_config() noexcept;\n    void apply_page_health(\n        const HudHealthSnapshot &health) noexcept;\n    void handle_webview_process_failure(\n""",
    "HUD page-health declaration",
)
path.write_text(text, encoding="utf-8")

path = root / "src" / "hud" / "hud-window.cpp"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    """    case kWebViewDocumentReadyMessage:\n        cancel_navigation_retry();\n        if (page_state_ != HudPageState::SetupRequired) {\n            set_page_health(\n                HudPageState::Ready, page_provider_);\n        }\n        update_host_state();\n        return 0L;\n    case kWebViewProcessFailedMessage:\n""",
    """    case kWebViewDocumentReadyMessage:\n        cancel_navigation_retry();\n        if (page_state_ != HudPageState::SetupRequired) {\n            apply_page_health(HudHealthSnapshot{\n                HudPageState::Loading,\n                page_provider_,\n                0U});\n        } else {\n            update_host_state();\n        }\n        return 0L;\n    case kWebViewPageHealthMessage:\n        apply_page_health(decode_hud_health(\n            static_cast<std::uint32_t>(wparam)));\n        return 0L;\n    case kWebViewProcessFailedMessage:\n""",
    "HUD WebView page-health dispatch",
)
text = replace_once(
    text,
    """void HudWindow::handle_webview_process_failure(\n""",
    """void HudWindow::apply_page_health(\n    const HudHealthSnapshot &health) noexcept\n{\n    if (!is_valid_hud_health(health) ||\n        !is_dom_reportable_page_state(health.state) ||\n        health.provider == HudProvider::Unknown ||\n        health.provider != page_provider_) {\n        return;\n    }\n\n    set_page_health(\n        health.state, health.provider, health.detail_code);\n    switch (health.state) {\n    case HudPageState::Loading:\n        navigation_status_ = L\"CHAT LOADING\";\n        navigation_tone_ = L\"#ffcc00\";\n        break;\n    case HudPageState::Ready:\n        cancel_navigation_retry();\n        navigation_status_.clear();\n        navigation_tone_ = L\"#ffcc00\";\n        break;\n    case HudPageState::LoginRequired:\n        cancel_navigation_retry();\n        navigation_status_ = L\"LOGIN REQUIRED\";\n        navigation_tone_ = L\"#ffcc00\";\n        break;\n    case HudPageState::Offline:\n        cancel_navigation_retry();\n        navigation_status_ = L\"BROADCAST OFFLINE\";\n        navigation_tone_ = L\"#aeb0b2\";\n        break;\n    case HudPageState::LayoutChanged:\n        cancel_navigation_retry();\n        navigation_status_ = L\"CHAT PAGE CHANGED\";\n        navigation_tone_ = L\"#ff3b30\";\n        break;\n    default:\n        return;\n    }\n    update_host_state();\n}\n\nvoid HudWindow::handle_webview_process_failure(\n""",
    "HUD page-health application",
)
path.write_text(text, encoding="utf-8")

# JavaScript syntax validation in Windows CI.
path = root / ".github" / "workflows" / "windows-build.yml"
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    """      - name: Check out pinned OBS Studio sources\n""",
    """      - name: Validate embedded JavaScript\n        shell: pwsh\n        run: |\n          $ErrorActionPreference = 'Stop'\n          python scripts/check-embedded-javascript.py `\n            src/hud/webview-host.cpp `\n            src/hud/page-health-message.cpp\n\n      - name: Check out pinned OBS Studio sources\n""",
    "embedded JavaScript CI validation",
)
path.write_text(text, encoding="utf-8")
