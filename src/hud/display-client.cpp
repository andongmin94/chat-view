// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/display-client.hpp"
#include "hud/saved-connection.hpp"
#include "common/win32-handle.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace chatview {
namespace {
constexpr size_t kMaxFrameBytes = 2U * 1024U * 1024U;
constexpr ULONGLONG kOperationMs = 15000U;
constexpr ULONGLONG kLeaseMs = 300000U;
struct InvalidFrame {};
struct NetworkFailure {};
struct HttpFailure { DWORD status; };
void require(bool ok) { if (!ok) throw InvalidFrame{}; }
void network(bool ok) { if (!ok) throw NetworkFailure{}; }
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
std::wstring hex(const std::array<unsigned char, 32> &bytes)
{
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result; result.reserve(64U);
    for (const auto value : bytes) { result += digits[value >> 4U]; result += digits[value & 15U]; }
    return result;
}
std::wstring login_verifier()
{
    std::array<unsigned char, 32> bytes{};
    require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
    auto result = hex(bytes); SecureZeroMemory(bytes.data(), bytes.size()); return result;
}
std::wstring login_challenge(const std::wstring &verifier)
{
    require(key(verifier));
    std::string ascii; ascii.reserve(verifier.size());
    for (const wchar_t value : verifier) ascii.push_back(static_cast<char>(value));
    std::array<unsigned char, 32> bytes{};
    const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(ascii.data()), static_cast<ULONG>(ascii.size()), bytes.data(), static_cast<ULONG>(bytes.size()));
    SecureZeroMemory(ascii.data(), ascii.size()); require(status >= 0);
    return hex(bytes);
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
    require(result.secure || (local && parts.nScheme == INTERNET_SCHEME_HTTP && result.host == L"127.0.0.1"));
    return result;
}
std::wstring utf16(std::string_view text)
{
    require(!text.empty() && text.size() <= kMaxFrameBytes && text.find('\0') == std::string_view::npos);
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    require(length > 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) == length);
    return result;
}
struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    explicit InternetHandle(HINTERNET handle) : value(handle) { network(value != nullptr); }
};
// Retain the receive buffer until HANDLE_CLOSING, including cancelled reads.
struct Completion {
    UniqueHandle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::shared_ptr<Completion> until_closed;
    std::array<char, 16384> buffer{};
    std::atomic<DWORD> status{0};
    std::atomic<DWORD> bytes{0};
    std::atomic<WINHTTP_WEB_SOCKET_BUFFER_TYPE> kind{WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE};
    bool websocket = false;
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void *information, DWORD length) noexcept
    {
        auto *self = reinterpret_cast<Completion *>(context);
        if (!self) return;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            const auto lifetime = std::move(self->until_closed); (void)lifetime; return;
        }
        if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
            status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
        if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            if (self->websocket) {
                if (!information || length != sizeof(WINHTTP_WEB_SOCKET_STATUS)) status = WINHTTP_CALLBACK_STATUS_REQUEST_ERROR;
                else {
                    const auto &value = *static_cast<WINHTTP_WEB_SOCKET_STATUS *>(information);
                    self->bytes.store(value.dwBytesTransferred); self->kind.store(value.eBufferType);
                }
            } else self->bytes.store(length);
        }
        self->status.store(status); SetEvent(self->event.get());
    }
};
class AsyncHandle final {
public:
    AsyncHandle(HINTERNET value, bool websocket = false) : value_(value), result_(std::make_shared<Completion>())
    {
        require(static_cast<bool>(result_->event)); result_->websocket = websocket;
        network(WinHttpSetStatusCallback(value_.value, Completion::callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(result_.get());
        result_->until_closed = result_;
        if (!WinHttpSetOption(value_.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
            result_->until_closed.reset(); network(false);
        }
    }
    AsyncHandle(const AsyncHandle &) = delete;
    AsyncHandle &operator=(const AsyncHandle &) = delete;
    HINTERNET get() const noexcept { return value_.value; }
    Completion &result() noexcept { return *result_; }
    void prepare() noexcept { result_->status.store(0); ResetEvent(result_->event.get()); }
    void wait(HANDLE cancel, ULONGLONG deadline, DWORD expected)
    {
        const ULONGLONG now = GetTickCount64(); network(now < deadline);
        HANDLE handles[] = {cancel, result_->event.get()};
        network(WaitForMultipleObjects(2, handles, FALSE,
            static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, kOperationMs))) == WAIT_OBJECT_0 + 1U);
        network(result_->status.load() == expected);
        network(WaitForSingleObject(cancel, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline);
    }
private:
    InternetHandle value_;
    std::shared_ptr<Completion> result_;
};
void send_request(AsyncHandle &request, const std::wstring &header, HANDLE cancel, ULONGLONG deadline, bool upgrade)
{
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    require(WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) != FALSE);
    if (upgrade) require(WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) != FALSE);
    require(WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(header.size()),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE);
    request.prepare();
    network(WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                               0, 0, reinterpret_cast<DWORD_PTR>(&request.result())) != FALSE);
    request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    request.prepare(); network(WinHttpReceiveResponse(request.get(), nullptr) != FALSE);
    request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    DWORD code = 0, size = sizeof(code);
    network(WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) != FALSE);
    if (code != (upgrade ? 101U : 200U)) throw HttpFailure{code};
}
std::string response_body(AsyncHandle &request, HANDLE cancel, ULONGLONG deadline)
{
    std::string body;
    for (;;) {
        request.prepare();
        network(WinHttpReadData(request.get(), request.result().buffer.data(),
                               static_cast<DWORD>(request.result().buffer.size()), nullptr) != FALSE);
        request.wait(cancel, deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        const DWORD count = request.result().bytes.load();
        require(count <= request.result().buffer.size() && count + body.size() <= 4096U);
        if (!count) return body;
        body.append(request.result().buffer.data(), count);
    }
}
winrt::Windows::Data::Json::JsonObject post(HINTERNET connection, DWORD flags, const wchar_t *path,
    const wchar_t *scheme, const std::wstring &credential, HANDLE cancel, ULONGLONG deadline, bool remember = false)
{
    AsyncHandle request(WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    std::wstring header = std::wstring(L"Authorization: ") + scheme + L" " + credential + L"\r\n";
    if (remember) header += L"X-ChatView-Remember: 1\r\n";
    SecretWipe wipe_header{header};
    send_request(request, header, cancel, deadline, false);
    std::string body = response_body(request, cancel, deadline);
    struct WipeBody { std::string &value; ~WipeBody() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); } } wipe_body{body};
    auto text = utf16(body);
    SecretWipe wipe{text};
    return winrt::Windows::Data::Json::JsonObject::Parse(text);
}
bool inspect_frame(const std::wstring &frame)
{
    constexpr std::wstring_view states[] = {L"idle", L"connecting", L"subscribing", L"subscribed", L"disconnected", L"revoked", L"unsubscribed", L"error", L"stopped"};
    const auto value = winrt::Windows::Data::Json::JsonObject::Parse(frame);
    require(value.GetNamedString(L"type") == L"chat-snapshot" && value.GetNamedNumber(L"version") == 1);
    const auto snapshot = value.GetNamedObject(L"snapshot"); const auto state = snapshot.GetNamedString(L"state");
    require(std::find(std::begin(states), std::end(states), std::wstring_view(state)) != std::end(states));
    const double count = snapshot.GetNamedNumber(L"received");
    require(std::isfinite(count) && count >= 0 && count <= 9007199254740991.0 &&
            std::floor(count) == count && snapshot.GetNamedArray(L"messages").Size() <= 100U);
    return state == L"subscribed";
}
}
struct DisplayClient::State {
    UniqueHandle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> finished{false};
    std::mutex mutex;
    DisplayUpdate latest{DisplayStatus::Connecting, {}, false};
    std::wstring login_url;
    bool pending = true;
    bool reusable = false;
    ULONGLONG expires = 0, last_frame = 0;
    void finish(DisplayStatus status) noexcept
    {
        std::lock_guard lock(mutex);
        latest = {status, {}, false}; login_url.clear(); pending = true; expires = 0; last_frame = 0;
    }
};
DisplayClient::~DisplayClient() { stop(); if (worker_.joinable()) worker_.join(); }
bool DisplayClient::start(std::wstring origin, std::wstring credential, bool local, DisplayAuthentication authentication) noexcept
{
    SecretWipe wipe{credential};
    try {
        if (running()) return false;
        if (worker_.joinable()) worker_.join();
        const bool browser = authentication == DisplayAuthentication::Browser || authentication == DisplayAuthentication::BrowserRemember;
        require(browser ? credential.empty() : key(credential)); (void)endpoint(origin, local);
        auto state = std::make_shared<State>(); require(static_cast<bool>(state->cancel));
        // Every successful login/exchange receives an in-run renewal session.
        // Remember/BrowserRemember additionally persist it on this Windows user.
        state->reusable = authentication != DisplayAuthentication::SignOut; state_ = state;
        worker_ = std::thread(&DisplayClient::run, state, std::move(origin), std::move(credential), local, authentication);
        return true;
    } catch (...) { if (state_) { state_->finish(DisplayStatus::Failed); state_->finished.store(true); } return false; }
}
void DisplayClient::stop() noexcept { if (state_) { SetEvent(state_->cancel.get()); state_->finish(DisplayStatus::Ended); } }
bool DisplayClient::take_login_url(std::wstring &url) noexcept
{
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    if (state_->login_url.empty() || WaitForSingleObject(state_->cancel.get(), 0) != WAIT_TIMEOUT) return false;
    url = std::move(state_->login_url); state_->login_url.clear(); return true;
}
bool DisplayClient::running() const noexcept { return state_ && !state_->finished.load(); }
bool DisplayClient::take(DisplayUpdate &update) noexcept
{
    if (!state_) return false;
    std::lock_guard lock(state_->mutex); const auto now = GetTickCount64();
    if (state_->latest.status == DisplayStatus::Receiving &&
        ((state_->expires && now >= state_->expires) || (state_->last_frame && now - state_->last_frame >= kOperationMs))) {
        if (!state_->reusable) SetEvent(state_->cancel.get());
        state_->latest = {state_->reusable ? DisplayStatus::Reconnecting : DisplayStatus::Ended, {}, false};
        state_->pending = true; state_->expires = 0; state_->last_frame = 0;
    }
    if (!state_->pending) return false;
    update = std::move(state_->latest);
    state_->latest.status = update.status; state_->latest.subscribed = update.subscribed; state_->pending = false;
    return true;
}
void DisplayClient::run(const std::shared_ptr<State> &state, std::wstring origin, std::wstring credential,
                        bool local, DisplayAuthentication authentication) noexcept
{
    SecretWipe wipe{credential}; bool apartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true;
        const auto target = endpoint(origin, local);
        InternetHandle session(WinHttpOpen(L"ChatView/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
        require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 15000) != FALSE);
        InternetHandle connection(WinHttpConnect(session.value, target.host.c_str(), target.port, 0));
        const DWORD flags = target.secure ? WINHTTP_FLAG_SECURE : 0;
        if (authentication == DisplayAuthentication::SignOut) {
            const auto result = post(connection.value, flags, L"/display/signout", L"ChatView-Session",
                credential, state->cancel.get(), GetTickCount64() + kOperationMs);
            require(result.GetNamedBoolean(L"signedOut"));
            state->finish(DisplayStatus::SignedOut);
        } else {
        std::optional<winrt::Windows::Data::Json::JsonObject> approved_lease;
        ULONGLONG approved_started = 0U;
        if (authentication == DisplayAuthentication::Browser || authentication == DisplayAuthentication::BrowserRemember) {
            const bool keep = authentication == DisplayAuthentication::BrowserRemember;
            std::wstring verifier = login_verifier(); SecretWipe wipe_verifier{verifier};
            const auto started = GetTickCount64();
            const auto request = post(connection.value, flags, L"/display/login", L"ChatView-Challenge",
                login_challenge(verifier), state->cancel.get(), started + kOperationMs, keep);
            const std::wstring id = request.GetNamedString(L"id").c_str();
            require(id.size() == 32U && std::all_of(id.begin(), id.end(), [](wchar_t c) {
                return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
            }));
            const std::wstring path = L"/login/" + id;
            require(std::wstring_view(request.GetNamedString(L"verificationPath")) == std::wstring_view(path));
            const double duration = request.GetNamedNumber(L"expiresInMs"), interval = request.GetNamedNumber(L"intervalMs");
            require(std::isfinite(duration) && duration > 0 && duration <= 300000 && std::floor(duration) == duration);
            require(std::isfinite(interval) && interval >= 1000 && interval <= 5000 && std::floor(interval) == interval);
            const auto deadline = started + static_cast<ULONGLONG>(duration);
            const std::wstring poll_path = L"/display/login/" + id;
            {
                std::lock_guard lock(state->mutex);
                network(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT);
                state->login_url = (origin.ends_with(L"/") ? origin.substr(0, origin.size() - 1U) : origin) + path;
                state->latest = {DisplayStatus::AwaitingLogin, {}, false}; state->pending = true;
            }
            while (!approved_lease) {
                network(GetTickCount64() < deadline);
                network(WaitForSingleObject(state->cancel.get(), static_cast<DWORD>(interval)) == WAIT_TIMEOUT);
                try {
                    const auto poll_started = GetTickCount64();
                    const auto result = post(connection.value, flags, poll_path.c_str(), L"ChatView-Login",
                        verifier, state->cancel.get(), std::min(deadline, GetTickCount64() + kOperationMs));
                    const auto status = result.GetNamedString(L"status");
                    if (status == L"approved") { approved_lease = result.GetNamedObject(L"lease"); approved_started = poll_started; }
                    else require(status == L"pending");
                } catch (const HttpFailure &error) {
                    if (error.status != 429) throw; // Server cadence is respected; never replay a consumed success.
                }
            }
            authentication = keep ? DisplayAuthentication::Remember : DisplayAuthentication::OneTime;
        }
        bool renewable = authentication == DisplayAuthentication::Saved;
        bool persisted = authentication == DisplayAuthentication::Saved;
        unsigned int failures = 0;
        while (WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT) {
            bool authorizing_session = true;
            try {
                const auto started = approved_lease ? approved_started : GetTickCount64();
                const auto lease = approved_lease ? *approved_lease : post(connection.value, flags,
                    renewable ? L"/display/refresh" : L"/display/exchange",
                    renewable ? L"ChatView-Session" : L"ChatView-Ticket", credential, state->cancel.get(), started + kOperationMs);
                approved_lease.reset();
                authorizing_session = false;
                require(lease.GetNamedString(L"scope") == L"chat:read");
                std::wstring token = lease.GetNamedString(L"token").c_str(); SecretWipe wipe_token{token}; require(key(token));
                const double duration = lease.GetNamedNumber(L"expiresInMs");
                require(std::isfinite(duration) && duration > 0 && duration <= static_cast<double>(kLeaseMs) && std::floor(duration) == duration);
                if (!renewable) {
                    require(lease.GetNamedString(L"sessionScope") == L"chat:renew");
                    std::wstring login = lease.GetNamedString(L"sessionToken").c_str(); SecretWipe wipe_login{login};
                    require(key(login));
                    const double lifetime = lease.GetNamedNumber(L"sessionExpiresInMs");
                    require(std::isfinite(lifetime) && lifetime > 0 && lifetime <= 2592000000.0 && std::floor(lifetime) == lifetime);
                    network(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT);
                    persisted = authentication == DisplayAuthentication::Remember;
                    if (persisted) {
                        require(save_connection({origin, login, local}));
                        if (WaitForSingleObject(state->cancel.get(), 0) != WAIT_TIMEOUT) {
                            forget_matching_connection(login); throw NetworkFailure{};
                        }
                    }
                    erase(credential); credential = std::move(login);
                    renewable = true;
                    { std::lock_guard lock(state->mutex); state->reusable = true; }
                }
                const auto expires = started + static_cast<ULONGLONG>(duration);
                const auto renew = started + static_cast<ULONGLONG>(duration * 0.8);
                network(GetTickCount64() < renew);
                AsyncHandle request(WinHttpOpenRequest(connection.value, L"GET", L"/display/events", nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
                std::wstring header = L"Authorization: Bearer " + token + L"\r\n"; SecretWipe wipe_header{header};
                send_request(request, header, state->cancel.get(), std::min(renew, GetTickCount64() + kOperationMs), true);
                AsyncHandle socket(WinHttpWebSocketCompleteUpgrade(request.get(), 0), true);
                erase(token); erase(header);
                { std::lock_guard lock(state->mutex); state->expires = expires; }
                auto deadline = std::min(renew, GetTickCount64() + kOperationMs);
                std::string frame;
                for (;;) {
                    network(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT);
                    if (renewable && GetTickCount64() >= renew) break;
                    socket.prepare();
                    network(WinHttpWebSocketReceive(socket.get(), socket.result().buffer.data(),
                        static_cast<DWORD>(socket.result().buffer.size()), nullptr, nullptr) == NO_ERROR);
                    try { socket.wait(state->cancel.get(), deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE); }
                    catch (const NetworkFailure &) { if (renewable && GetTickCount64() >= renew) break; throw; }
                    const auto kind = socket.result().kind.load();
                    if (kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) throw NetworkFailure{};
                    require(kind == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || kind == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
                    const DWORD count = socket.result().bytes.load();
                    require(count <= socket.result().buffer.size() && count + frame.size() <= kMaxFrameBytes);
                    frame.append(socket.result().buffer.data(), count);
                    if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
                    auto envelope = utf16(frame); frame.clear(); const bool subscribed = inspect_frame(envelope);
                    {
                        std::lock_guard lock(state->mutex);
                        network(WaitForSingleObject(state->cancel.get(), 0) == WAIT_TIMEOUT && GetTickCount64() < expires);
                        state->latest = {DisplayStatus::Receiving, std::move(envelope), subscribed};
                        state->last_frame = GetTickCount64(); state->pending = true;
                    }
                    failures = 0; deadline = std::min(renew, GetTickCount64() + kOperationMs);
                }
                // RAII closes the old socket before minting its replacement.
                continue;
            } catch (const HttpFailure &error) {
                if (renewable && authorizing_session && (error.status == 401 || error.status == 403)) {
                    if (persisted) forget_matching_connection(credential);
                    state->finish(DisplayStatus::Denied); break;
                }
                if (!renewable || (error.status != 429 && error.status < 500 &&
                    (authorizing_session || (error.status != 401 && error.status != 403)))) throw;
            } catch (const NetworkFailure &) { if (!renewable) throw; }
            if (WaitForSingleObject(state->cancel.get(), 0) != WAIT_TIMEOUT) break;
            state->finish(DisplayStatus::Reconnecting);
            const DWORD delay = std::min<DWORD>(30000U, 1000U << std::min(failures++, 5U));
            if (WaitForSingleObject(state->cancel.get(), delay) != WAIT_TIMEOUT) break;
        }
        if (WaitForSingleObject(state->cancel.get(), 0) == WAIT_OBJECT_0) state->finish(DisplayStatus::Ended);
        }
    } catch (...) {
        // Never log exception text, URL, credential, server error body or chat.
        state->finish(WaitForSingleObject(state->cancel.get(), 0) == WAIT_OBJECT_0 ? DisplayStatus::Ended : DisplayStatus::Failed);
    }
    if (apartment) winrt::uninit_apartment();
    state->finished.store(true);
}
}
