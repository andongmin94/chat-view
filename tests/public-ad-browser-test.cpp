// SPDX-License-Identifier: GPL-2.0-or-later
// Actual browser document/HTTP rendering, NOT an OBS capture or exposure test.
#include <Windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        expect(message.message != WM_QUIT, "browser test window unexpectedly closed");
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
void await(const std::function<bool()> &predicate, const char *message, ULONGLONG timeout = 10000)
{
    const auto deadline = GetTickCount64() + timeout;
    while (!predicate()) {
        expect(GetTickCount64() < deadline, message); pump();
        MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
void command(const char *expected)
{
    std::string value; std::getline(std::cin, value); expect(value == expected, "fixture phase mismatch");
}
void signal(const char *value) { std::cout << value << '\n' << std::flush; }
struct Browser {
    HWND window = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> core;
    ~Browser() { if (controller) controller->Close(); if (window) DestroyWindow(window); }
};
bool evaluate(ICoreWebView2 *core, const wchar_t *script, ULONGLONG timeout = 10000)
{
    struct Result { bool done = false; HRESULT code = E_FAIL; std::wstring value; };
    const auto result = std::make_shared<Result>();
    static unsigned script_sequence = 0;
    const unsigned request = ++script_sequence;
    expect(SUCCEEDED(core->ExecuteScript(script,
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [result](HRESULT code, LPCWSTR value) -> HRESULT {
                result->code = code;
                try { if (value) result->value = value; }
                catch (...) { result->code = E_OUTOFMEMORY; }
                result->done = true; return S_OK;
            }).Get())), "execute browser assertion");
    try { await([&] { return result->done; }, "browser assertion callback", timeout); }
    catch (...) {
        std::cerr << "Public ad script evidence: request=" << request
            << " completed=" << result->done << " hresult=" << result->code << '\n';
        throw;
    }
    expect(SUCCEEDED(result->code), "browser assertion returned");
    return result->value == L"true";
}
}
int main()
{
    try {
        expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM startup");
        struct Com { ~Com() { CoUninitialize(); } } com;
        std::string input; std::getline(std::cin, input);
        expect(input.size() < 2048 && input.starts_with("http://127.0.0.1:") &&
            input.find("/public/ads/") != std::string::npos && input.find('?') == std::string::npos,
            "fixture supplies only a public source URL");
        const std::wstring url(input.begin(), input.end());
        wchar_t temporary[MAX_PATH]{}; expect(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory");
        const auto profile = std::wstring(temporary) + L"ChatView-PublicAd-" + std::to_wstring(GetCurrentProcessId());
        expect(CreateDirectoryW(profile.c_str(), nullptr) != FALSE, "isolated browser profile");
        const auto browser = std::make_shared<Browser>();
        browser->window = CreateWindowExW(0, L"STATIC", L"ChatView public ad regression",
            WS_POPUP | WS_VISIBLE, 0, 0, 960, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        expect(browser->window != nullptr, "browser host window");
        struct Startup { bool done = false; HRESULT code = E_FAIL; };
        const auto startup = std::make_shared<Startup>();
        expect(SUCCEEDED(CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [browser, startup](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                    if (FAILED(result) || !environment) { startup->code = result; startup->done = true; return S_OK; }
                    const auto queued = environment->CreateCoreWebView2Controller(browser->window,
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [browser, startup](HRESULT code, ICoreWebView2Controller *controller) -> HRESULT {
                                startup->code = code;
                                if (SUCCEEDED(code) && controller) {
                                    browser->controller = controller;
                                    startup->code = controller->get_CoreWebView2(&browser->core);
                                }
                                startup->done = true; return S_OK;
                            }).Get());
                    if (FAILED(queued)) { startup->code = queued; startup->done = true; }
                    return S_OK;
                }).Get())), "create isolated WebView2 environment");
        await([&] { return startup->done; }, "browser startup", 20000);
        expect(SUCCEEDED(startup->code) && browser->core && browser->controller, "browser created");
        expect(SUCCEEDED(browser->controller->put_Bounds(RECT{0, 0, 960, 180})), "banner viewport");
        expect(SUCCEEDED(browser->controller->put_IsVisible(TRUE)), "visible browser");
        Microsoft::WRL::ComPtr<ICoreWebView2Controller2> transparent;
        expect(SUCCEEDED(browser->controller.As(&transparent)), "transparent browser controller");
        expect(SUCCEEDED(transparent->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0})), "transparent background");
        // Navigate is asynchronous. Do not issue DOM scripts against the
        // outgoing initial document while the public page is being installed.
        // Match NavigationId: an overlapping about:blank completion is not ours.
        struct Navigation {
            std::wstring expected;
            UINT64 id = 0;
            bool started = false, done = false;
            BOOL success = FALSE;
            HRESULT code = S_OK;
            COREWEBVIEW2_WEB_ERROR_STATUS error = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        };
        const auto navigation = std::make_shared<Navigation>(); navigation->expected = url;
        struct NavigationHandlers {
            ICoreWebView2 *core;
            EventRegistrationToken starting{}, completed{};
            bool has_starting = false, has_completed = false;
            ~NavigationHandlers()
            {
                if (has_completed) core->remove_NavigationCompleted(completed);
                if (has_starting) core->remove_NavigationStarting(starting);
            }
        } handlers{browser->core.Get()};
        expect(SUCCEEDED(browser->core->add_NavigationStarting(
            Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                [navigation](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    navigation->code = args->get_Uri(&uri);
                    const bool match = SUCCEEDED(navigation->code) && uri && navigation->expected == uri;
                    CoTaskMemFree(uri);
                    if (match) {
                        navigation->code = args->get_NavigationId(&navigation->id);
                        navigation->started = SUCCEEDED(navigation->code);
                    }
                    if (FAILED(navigation->code)) navigation->done = true;
                    return S_OK;
                }).Get(), &handlers.starting)), "observe public navigation start");
        handlers.has_starting = true;
        expect(SUCCEEDED(browser->core->add_NavigationCompleted(
            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [navigation](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    UINT64 id = 0;
                    const HRESULT code = args->get_NavigationId(&id);
                    if (FAILED(code)) { navigation->code = code; navigation->done = true; return S_OK; }
                    if (!navigation->started || id != navigation->id) return S_OK;
                    navigation->code = args->get_IsSuccess(&navigation->success);
                    const HRESULT error = args->get_WebErrorStatus(&navigation->error);
                    if (FAILED(error)) navigation->code = error;
                    navigation->done = true; return S_OK;
                }).Get(), &handlers.completed)), "observe public navigation completion");
        handlers.has_completed = true;
        // The navigation gate and original DOM condition SHARE the previous
        // ten-second page-load deadline; no extra retry or callback budget.
        const auto load_deadline = GetTickCount64() + 10000;
        const auto remaining = [&] {
            const auto now = GetTickCount64(); expect(now < load_deadline, "public page load");
            return load_deadline - now;
        };
        try {
            expect(SUCCEEDED(browser->core->Navigate(url.c_str())), "load public renderer, not management UI");
            await([&] { return navigation->done; }, "public navigation completion", remaining());
            expect(SUCCEEDED(navigation->code) && navigation->success, "public navigation succeeded");
            // This executes while the HTTP fixture is deliberately holding
            // ad-source.js. The first browser document must already be a fully
            // loaded, transparent, text-free page, with no state read or artwork.
            // The module may start only after the fixture releases its response.
            await([&] { return evaluate(browser->core.Get(),
                L"document.readyState === 'complete' && !!document.getElementById('banner') && "
                L"document.getElementById('banner').hidden && "
                L"document.getElementById('title').textContent === '' && "
                L"getComputedStyle(document.body).backgroundColor === 'rgba(0, 0, 0, 0)'",
                remaining()); }, "transparent first public DOM without executable asset response", remaining());
        } catch (...) {
            // Phase/IDs/errors only, never URL, account, script result or DOM.
            std::cerr << "Public ad navigation evidence: started=" << navigation->started
                << " completed=" << navigation->done << " id=" << navigation->id
                << " success=" << navigation->success << " hresult=" << navigation->code
                << " web_error=" << static_cast<int>(navigation->error) << '\n';
            throw;
        }
        expect(evaluate(browser->core.Get(), L"document.getElementById('banner').hidden && getComputedStyle(document.body).backgroundColor === 'rgba(0, 0, 0, 0)'"), "starts transparent without a sender report");
        signal("loaded"); command("reported");
        const wchar_t *visible = L"!document.getElementById('banner').hidden && document.getElementById('brand').textContent === 'ChatView'";
        await([&] { return evaluate(browser->core.Get(), visible); }, "live report permits test artwork");
        expect(evaluate(browser->core.Get(), LR"JS(
            document.getElementById('title').textContent === '방송과 함께하는 챗뷰' &&
            document.querySelector('.badge').textContent === '시험 광고 · 지급 없음' &&
            document.querySelectorAll('img,iframe,form').length === 0 &&
            document.documentElement.scrollWidth === innerWidth &&
            document.documentElement.scrollHeight === innerHeight &&
            !document.body.textContent.includes('alice-private')
        )JS"), "actual DOM has bounded public test artwork only");
        // Synthetic page-lifecycle events exercise the actual entry module in
        // WebView2; they are not evidence of a real OBS scene transition.
        expect(evaluate(browser->core.Get(), LR"JS(
            dispatchEvent(new PageTransitionEvent('pagehide', {persisted:true}));
            document.getElementById('banner').hidden && document.getElementById('title').textContent === ''
        )JS"), "page hide cancels polling and clears artwork");
        expect(evaluate(browser->core.Get(), LR"JS(
            dispatchEvent(new PageTransitionEvent('pageshow', {persisted:true}));
            document.getElementById('banner').hidden
        )JS"), "page restore stays hidden until a fresh response");
        await([&] { return evaluate(browser->core.Get(), visible); }, "restored page starts a fresh public read");
        signal("visible"); command("stopped");
        const wchar_t *hidden = L"document.getElementById('banner').hidden && document.getElementById('title').textContent === ''";
        await([&] { return evaluate(browser->core.Get(), hidden); }, "stop clears the actual rendered banner");
        signal("hidden"); command("selected");
        await([&] { return evaluate(browser->core.Get(), visible); }, "explicit selection restores the same public source");
        signal("visible-again"); command("outage");
        await([&] { return evaluate(browser->core.Get(), hidden); }, "independent deadline hides artwork despite hung public requests", 18000);
        signal("expired"); command("finish");
        std::cout << "Public ad DOM, selection, stop and outage expiry passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "Public ad browser test failed: " << error.what() << '\n'; return 1; }
}
