// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/display-client.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace chatview {
namespace {
constexpr size_t kMaxFrameBytes = 2U * 1024U * 1024U;
constexpr ULONGLONG kOperationMs = 15000U;
constexpr ULONGLONG kLeaseMs = 300000U;
void require(bool ok) { if (!ok) throw std::runtime_error("Display connection failed"); }
void erase(std::wstring &text) noexcept
{
    if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
    text.clear();
}
struct SecretWipe { std::wstring &value; ~SecretWipe() { erase(value); } };
bool key(std::wstring_view value) noexcept
{
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
    });
}
struct Endpoint { std::wstring host; INTERNET_PORT port = 0; bool secure = true; };
Endpoint endpoint(const std::wstring &origin, bool local)
{
    require(!origin.empty() && origin.size() <= 2048U);
    require(std::all_of(origin.begin(), origin.end(), [](wchar_t c) {
        return c > 32 && c < 127 && c != L'\\' && c != L'%' && c != L'?' && c != L'#';
    }));
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength =
        parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    require(WinHttpCrackUrl(origin.c_str(), static_cast<DWORD>(origin.size()), 0, &parts) != FALSE);
    require(parts.dwHostNameLength > 0 && parts.dwUserNameLength == 0 &&
            parts.dwPasswordLength == 0 && parts.dwExtraInfoLength == 0);
    const std::wstring_view path(parts.lpszUrlPath ? parts.lpszUrlPath : L"", parts.dwUrlPathLength);
    require(path.empty() || path == L"/");
    Endpoint result{std::wstring(parts.lpszHostName, parts.dwHostNameLength), parts.nPort,
                    parts.nScheme == INTERNET_SCHEME_HTTPS};
    require(result.port != 0);
    require(result.secure || (local && parts.nScheme == INTERNET_SCHEME_HTTP &&
                              result.host == L"127.0.0.1"));
    return result;
}
std::wstring utf16(std::string_view text)
{
    require(!text.empty() && text.size() <= kMaxFrameBytes && text.find('\0') == std::string_view::npos);
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    require(length > 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), result.data(), length) == length);
    return result;
}
struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    explicit InternetHandle(HINTERNET handle) : value(handle) { require(value != nullptr); }
};

// A callback owns no UI/owner pointer. Its self-reference retains the receive
// buffer until HANDLE_CLOSING, including cancellation with a read outstanding.
struct Completion {
    UniqueHandle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::shared_ptr<Completion> until_closed;
    std::array<char, 16384> buffer{};
    std::atomic<DWORD> status{0};
    std::atomic<DWORD> bytes{0};
    std::atomic<WINHTTP_WEB_SOCKET_BUFFER_TYPE> kind{WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE};
    bool websocket = false;
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status,
                                  void *information, DWORD length) noexcept
    {
        auto *self = reinterpret_cast<Completion *>(context);
        if (!self) return;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            const auto lifetime = std::move(self->until_closed);
            (void)lifetime;
            return;
        }
        if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
            status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
            status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
            status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
        if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            if (self->websocket) {
                if (!information || length != sizeof(WINHTTP_WEB_SOCKET_STATUS)) {
                    status = WINHTTP_CALLBACK_STATUS_REQUEST_ERROR;
                } else {
                    const auto &value = *static_cast<WINHTTP_WEB_SOCKET_STATUS *>(information);
                    self->bytes.store(value.dwBytesTransferred);
                    self->kind.store(value.eBufferType);
                }
            } else self->bytes.store(length);
        }
        self->status.store(status);
        SetEvent(self->event.get());
    }
};
class AsyncHandle final {
public:
    AsyncHandle(HINTERNET value, bool websocket = false) : value_(value), result_(std::make_shared<Completion>())
    {
        {
            require(static_cast<bool>(result_->event));
            result_->websocket = websocket;
            require(WinHttpSetStatusCallback(value_.value, Completion::callback,
                WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK);
            DWORD_PTR context = reinterpret_cast<DWORD_PTR>(result_.get());
            result_->until_closed = result_;
            if (!WinHttpSetOption(value_.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
                result_->until_closed.reset();
                require(false);
            }
        }
    }
    ~AsyncHandle() = default;
    AsyncHandle(const AsyncHandle &) = delete;
    AsyncHandle &operator=(const AsyncHandle &) = delete;
    HINTERNET get() const noexcept { return value_.value; }
    Completion &result() noexcept { return *result_; }
    void prepare() noexcept { result_->status.store(0); ResetEvent(result_->event.get()); }
    void wait(HANDLE cancel, ULONGLONG deadline, DWORD expected)
    {
        const ULONGLONG now = GetTickCount64(); require(now < deadline);
        HANDLE handles[] = {cancel, result_->event.get()};
        require(WaitForMultipleObjects(2, handles, FALSE,
            static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, kOperationMs))) == WAIT_OBJECT_0 + 1U);
        require(result_->status.load() == expected);
        require(WaitForSingleObject(cancel, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline);
    }
private:
    InternetHandle value_;
    std::shared_ptr<Completion> result_;
};
void send_request(AsyncHandle &request, const std::wstring &header, HANDLE cancel,
                  ULONGLONG deadline, bool upgrade)
{
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    require(WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) != FALSE);
    if (upgrade) require(WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) != FALSE);
    require(WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(header.size()),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE);
    request.prepare();
    require(WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                               0, 0, reinterpret_cast<DWORD_PTR>(&request.result())) != FALSE);
    request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    request.prepare();
    require(WinHttpReceiveResponse(request.get(), nullptr) != FALSE);
    request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    DWORD code = 0; DWORD size = sizeof(code);
    require(WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) != FALSE);
    require(code == (upgrade ? 101U : 200U));
}
std::string response_body(AsyncHandle &request, HANDLE cancel, ULONGLONG deadline)
{
    std::string body;
    for (;;) {
        request.prepare();
        require(WinHttpReadData(request.get(), request.result().buffer.data(),
                               static_cast<DWORD>(request.result().buffer.size()), nullptr) != FALSE);
        request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        const DWORD count = request.result().bytes.load();
        require(count <= request.result().buffer.size() && count + body.size() <= 4096U);
        if (!count) return body;
        body.append(request.result().buffer.data(), count);
    }
}
bool valid_state(std::wstring_view value) noexcept
{
    constexpr std::wstring_view states[] = {L"idle", L"connecting", L"subscribing", L"subscribed",
        L"disconnected", L"revoked", L"unsubscribed", L"error", L"stopped"};
    return std::find(std::begin(states), std::end(states), value) != std::end(states);
}
// Use Windows' JSON parser instead of introducing a second handwritten parser.
// Full display-field validation remains in the shared immutable JS receiver.
bool inspect_frame(const std::wstring &frame)
{
    using winrt::Windows::Data::Json::JsonObject;
    const auto value = JsonObject::Parse(frame);
    require(value.GetNamedString(L"type") == L"chat-snapshot" && value.GetNamedNumber(L"version") == 1);
    const auto snapshot = value.GetNamedObject(L"snapshot");
    const auto state = snapshot.GetNamedString(L"state");
    require(valid_state(std::wstring_view(state)));
    const double count = snapshot.GetNamedNumber(L"received");
    const auto rows = snapshot.GetNamedArray(L"messages");
    require(std::isfinite(count) && count >= 0 && count <= 9007199254740991.0 &&
            std::floor(count) == count && rows.Size() <= 100U);
    return state == L"subscribed";
}
}

struct DisplayClient::State {
    UniqueHandle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> finished{false};
    std::mutex mutex;
    DisplayUpdate latest{DisplayStatus::Connecting, {}, false};
    bool pending = true;
    ULONGLONG expires = 0;
    ULONGLONG last_frame = 0;
    void finish(DisplayStatus status) noexcept
    {
        std::lock_guard lock(mutex);
        latest = {status, {}, false}; pending = true;
    }
};
DisplayClient::~DisplayClient()
{
    stop();
    if (worker_.joinable()) worker_.join();
}
bool DisplayClient::start(std::wstring origin, std::wstring ticket, bool local) noexcept
{
    SecretWipe wipe{ticket};
    try {
        if (running()) return false;
        if (worker_.joinable()) worker_.join();
        require(key(ticket)); (void)endpoint(origin, local);
        auto state = std::make_shared<State>(); require(static_cast<bool>(state->cancel));
        state_ = state;
        // Capture ownership, not this: completion/cancellation cannot access a
        // destroyed UI object, and only this worker closes WinHTTP handles.
        worker_ = std::thread(&DisplayClient::run, state, std::move(origin), std::move(ticket), local);
        return true;
    } catch (...) {
        if (state_) { state_->finish(DisplayStatus::Failed); state_->finished.store(true); }
        return false;
    }
}
void DisplayClient::stop() noexcept
{
    if (!state_) return;
    SetEvent(state_->cancel.get()); state_->finish(DisplayStatus::Ended);
}
bool DisplayClient::running() const noexcept { return state_ && !state_->finished.load(); }
bool DisplayClient::take(DisplayUpdate &update) noexcept
{
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    const auto now = GetTickCount64();
    if (state_->latest.status == DisplayStatus::Receiving &&
        ((state_->expires && now >= state_->expires) ||
         (state_->last_frame && now - state_->last_frame >= kOperationMs))) {
        SetEvent(state_->cancel.get());
        state_->latest = {DisplayStatus::Ended, {}, false}; state_->pending = true;
    }
    if (!state_->pending) return false;
    update = std::move(state_->latest);
    // Retain lifecycle status for synchronous expiry enforcement after the frame
    // has been consumed, without retaining a second message-history copy.
    state_->latest.status = update.status; state_->latest.subscribed = update.subscribed;
    state_->pending = false;
    return true;
}
void DisplayClient::run(const std::shared_ptr<State> &state, std::wstring origin,
                        std::wstring ticket, bool local) noexcept
{
    SecretWipe wipe_ticket{ticket};
    bool apartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true;
        const Endpoint target = endpoint(origin, local);
        InternetHandle session(WinHttpOpen(L"ChatView/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
        require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 15000) != FALSE);
        InternetHandle connection(WinHttpConnect(session.value, target.host.c_str(), target.port, 0));
        const DWORD flags = target.secure ? WINHTTP_FLAG_SECURE : 0;
        const ULONGLONG exchange_started = GetTickCount64();
        std::wstring token; SecretWipe wipe_token{token};
        ULONGLONG expires = 0;
        {
            AsyncHandle request(WinHttpOpenRequest(connection.value, L"POST", L"/display/exchange", nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            std::wstring header = L"Authorization: ChatView-Ticket " + ticket + L"\r\n";
            SecretWipe wipe_header{header};
            send_request(request, header, state->cancel.get(), exchange_started + kOperationMs, false);
            std::string body = response_body(request, state->cancel.get(), exchange_started + kOperationMs);
            const auto lease = winrt::Windows::Data::Json::JsonObject::Parse(utf16(body));
            SecureZeroMemory(body.data(), body.size()); body.clear();
            require(lease.GetNamedString(L"scope") == L"chat:read");
            token = lease.GetNamedString(L"token").c_str(); require(key(token));
            const double duration = lease.GetNamedNumber(L"expiresInMs");
            require(std::isfinite(duration) && duration > 0 && duration <= static_cast<double>(kLeaseMs) &&
                    std::floor(duration) == duration);
            expires = exchange_started + static_cast<ULONGLONG>(duration);
            require(GetTickCount64() < expires);
        }
        erase(ticket);
        AsyncHandle request(WinHttpOpenRequest(connection.value, L"GET", L"/display/events", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        std::wstring header = L"Authorization: Bearer " + token + L"\r\n";
        SecretWipe wipe_header{header};
        send_request(request, header, state->cancel.get(), std::min(expires, GetTickCount64() + kOperationMs), true);
        AsyncHandle socket(WinHttpWebSocketCompleteUpgrade(request.get(), 0), true);
        erase(token); erase(header);
        {
            std::lock_guard lock(state->mutex); state->expires = expires;
        }
        ULONGLONG frame_deadline = std::min(expires, GetTickCount64() + kOperationMs);
        std::string frame;
        for (;;) {
            require(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT);
            socket.prepare();
            require(WinHttpWebSocketReceive(socket.get(), socket.result().buffer.data(),
                static_cast<DWORD>(socket.result().buffer.size()), nullptr, nullptr) == NO_ERROR);
            socket.wait(state->cancel.get(), frame_deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            const auto kind = socket.result().kind.load();
            require(kind == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
                    kind == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
            const DWORD count = socket.result().bytes.load();
            require(count <= socket.result().buffer.size() && count + frame.size() <= kMaxFrameBytes);
            frame.append(socket.result().buffer.data(), count);
            if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
            auto envelope = utf16(frame); frame.clear();
            const bool subscribed = inspect_frame(envelope);
            {
                std::lock_guard lock(state->mutex);
                require(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT && GetTickCount64() < expires);
                state->latest = {DisplayStatus::Receiving, std::move(envelope), subscribed};
                state->last_frame = GetTickCount64(); state->pending = true;
            }
            frame_deadline = std::min(expires, GetTickCount64() + kOperationMs);
        }
    } catch (...) {
        // No exception text, URL, token, close reason or provider content in logs.
        state->finish(WaitForSingleObject(state->cancel.get(), 0) == WAIT_OBJECT_0
            ? DisplayStatus::Ended : DisplayStatus::Failed);
    }
    if (apartment) winrt::uninit_apartment();
    state->finished.store(true);
}
} // namespace chatview
