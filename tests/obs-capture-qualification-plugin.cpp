// SPDX-License-Identifier: GPL-2.0-or-later

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

OBS_DECLARE_MODULE()

namespace {

constexpr wchar_t kResultEnvironmentVariable[] =
    L"CHATVIEW_OBS_CAPTURE_QUALIFICATION_RESULT";
constexpr wchar_t kProbeWindowClassName[] =
    L"ChatViewObsCaptureQualificationProbe";
constexpr char kDisplayCaptureSourceId[] = "monitor_capture";
constexpr DWORD kSourceReadyTimeoutMs = 12000U;
constexpr DWORD kProbeSettleMs = 450U;
constexpr int kMinimumCalibrationDistance = 72;
constexpr int kHiddenDistanceFloor = 24;

#ifndef WDA_EXCLUDEFROMCAPTURE
constexpr DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
#endif

struct Pixel {
    int first = 0;
    int second = 0;
    int third = 0;
};

struct ProbeWindowData {
    COLORREF color = RGB(0, 0, 0);
};

struct SceneFixture {
    std::array<char, 128U> monitor_id{};
    std::array<char, 128U> source_name{};
    std::array<char, 256U> error{};
    obs_source_t *scene_source = nullptr;
    obs_source_t *display_source = nullptr;
    obs_sceneitem_t *item = nullptr;
    std::uint32_t base_width = 0U;
    std::uint32_t base_height = 0U;
    bool manual_showing = false;
};

std::atomic_bool stopping{false};
std::atomic_bool qualification_started{false};
std::thread qualification_thread;
std::wstring result_path;
bool frontend_callback_registered = false;

void set_fixture_error(SceneFixture &fixture, const char *message) noexcept
{
    if (fixture.error.front() != '\0') {
        return;
    }
    strncpy_s(
        fixture.error.data(),
        fixture.error.size(),
        message,
        _TRUNCATE);
}

std::wstring environment_value(const wchar_t *name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0U);
    if (required == 0U) {
        return {};
    }

    std::wstring value(required, L'\0');
    const DWORD length =
        GetEnvironmentVariableW(name, value.data(), required);
    if (length == 0U || length >= required) {
        return {};
    }
    value.resize(length);
    return value;
}

std::string utf8_from_wide(const wchar_t *value)
{
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 1) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        -1,
        converted.data(),
        required,
        nullptr,
        nullptr);
    if (written != required) {
        return {};
    }
    converted.resize(static_cast<std::size_t>(required - 1));
    return converted;
}

bool write_result_atomically(const std::string &content) noexcept
{
    try {
        if (result_path.empty()) {
            return false;
        }

        const std::filesystem::path destination(result_path);
        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
        }

        std::filesystem::path temporary = destination;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());

        {
            std::ofstream stream(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!stream) {
                return false;
            }
            stream.write(
                content.data(),
                static_cast<std::streamsize>(content.size()));
            stream.flush();
            if (!stream) {
                return false;
            }
        }

        if (!MoveFileExW(
                temporary.c_str(),
                destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void pump_probe_messages() noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK probe_window_proc(
    HWND window, UINT message, WPARAM, LPARAM lparam)
{
    auto *data = reinterpret_cast<ProbeWindowData *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<const CREATESTRUCTW *>(lparam);
        data = static_cast<ProbeWindowData *>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(data));
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1L;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const HBRUSH brush = CreateSolidBrush(
            data != nullptr ? data->color : RGB(0, 0, 0));
        FillRect(device, &client, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0L;
    }
    default:
        return DefWindowProcW(window, message, 0U, lparam);
    }
}

class ProbeWindows final {
public:
    ProbeWindows() = default;
    ~ProbeWindows()
    {
        close();
    }

    ProbeWindows(const ProbeWindows &) = delete;
    ProbeWindows &operator=(const ProbeWindows &) = delete;

    bool create(std::string &error)
    {
        instance_ = GetModuleHandleW(nullptr);
        if (instance_ == nullptr) {
            error = "GetModuleHandleW failed";
            return false;
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &probe_window_proc;
        window_class.hInstance = instance_;
        window_class.lpszClassName = kProbeWindowClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (RegisterClassExW(&window_class) == 0U &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            error = "RegisterClassExW failed";
            return false;
        }
        class_registered_ = true;

        const HMONITOR monitor = MonitorFromPoint(
            POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (monitor == nullptr ||
            !GetMonitorInfoW(
                monitor,
                reinterpret_cast<MONITORINFO *>(&monitor_info))) {
            error = "Primary monitor could not be resolved";
            return false;
        }
        monitor_id_ = utf8_from_wide(monitor_info.szDevice);
        if (monitor_id_.empty()) {
            error = "Primary monitor identifier could not be encoded";
            return false;
        }

        const RECT monitor_rect = monitor_info.rcMonitor;
        const int center_x =
            monitor_rect.left +
            (monitor_rect.right - monitor_rect.left) / 2;
        const int center_y =
            monitor_rect.top +
            (monitor_rect.bottom - monitor_rect.top) / 2;

        constexpr DWORD extended_style =
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        background_ = CreateWindowExW(
            extended_style,
            kProbeWindowClassName,
            L"ChatView capture qualification background",
            WS_POPUP,
            center_x - 180,
            center_y - 180,
            360,
            360,
            nullptr,
            nullptr,
            instance_,
            &background_data_);
        foreground_ = CreateWindowExW(
            extended_style,
            kProbeWindowClassName,
            L"ChatView capture qualification foreground",
            WS_POPUP,
            center_x - 90,
            center_y - 90,
            180,
            180,
            nullptr,
            nullptr,
            instance_,
            &foreground_data_);
        if (background_ == nullptr || foreground_ == nullptr) {
            error = "Probe windows could not be created";
            return false;
        }

        ShowWindow(background_, SW_HIDE);
        ShowWindow(foreground_, SW_HIDE);
        return true;
    }

    void close() noexcept
    {
        if (foreground_ != nullptr) {
            ShowWindow(foreground_, SW_HIDE);
            DestroyWindow(foreground_);
            foreground_ = nullptr;
        }
        if (background_ != nullptr) {
            ShowWindow(background_, SW_HIDE);
            DestroyWindow(background_);
            background_ = nullptr;
        }
        pump_probe_messages();

        if (class_registered_ && instance_ != nullptr) {
            UnregisterClassW(kProbeWindowClassName, instance_);
        }
        class_registered_ = false;
        instance_ = nullptr;
    }

    bool show_background_only(std::string &error)
    {
        if (background_ == nullptr || foreground_ == nullptr) {
            error = "Probe windows are unavailable";
            return false;
        }

        ShowWindow(foreground_, SW_HIDE);
        if (!SetWindowPos(
                background_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                    SWP_SHOWWINDOW)) {
            error = "Background probe could not be shown";
            return false;
        }
        return settle(error);
    }

    bool show_foreground_unprotected(std::string &error)
    {
        if (!SetWindowDisplayAffinity(foreground_, WDA_NONE)) {
            error = "Foreground capture affinity could not be reset";
            return false;
        }
        if (!SetWindowPos(
                foreground_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                    SWP_SHOWWINDOW)) {
            error = "Foreground probe could not be shown";
            return false;
        }
        return settle(error);
    }

    bool protect_foreground(std::string &error)
    {
        if (!SetWindowDisplayAffinity(
                foreground_, WDA_EXCLUDEFROMCAPTURE)) {
            error = "WDA_EXCLUDEFROMCAPTURE could not be enabled";
            return false;
        }
        return settle(error);
    }

    bool hide_foreground(std::string &error)
    {
        ShowWindow(foreground_, SW_HIDE);
        return settle(error);
    }

    [[nodiscard]] const std::string &monitor_id() const noexcept
    {
        return monitor_id_;
    }

private:
    bool settle(std::string &error)
    {
        pump_probe_messages();
        if (IsWindowVisible(background_)) {
            RedrawWindow(
                background_,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW |
                    RDW_ALLCHILDREN);
        }
        if (IsWindowVisible(foreground_)) {
            RedrawWindow(
                foreground_,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW |
                    RDW_ALLCHILDREN);
        }

        if (FAILED(DwmFlush())) {
            error = "DwmFlush failed while settling the probe";
            return false;
        }
        Sleep(kProbeSettleMs);
        pump_probe_messages();
        if (FAILED(DwmFlush())) {
            error = "DwmFlush failed after settling the probe";
            return false;
        }
        return true;
    }

    HINSTANCE instance_ = nullptr;
    HWND background_ = nullptr;
    HWND foreground_ = nullptr;
    bool class_registered_ = false;
    ProbeWindowData background_data_{RGB(24, 191, 83)};
    ProbeWindowData foreground_data_{RGB(226, 39, 139)};
    std::string monitor_id_;
};

void create_scene_fixture(void *opaque) noexcept
{
    auto &fixture = *static_cast<SceneFixture *>(opaque);

    obs_video_info video_info{};
    if (!obs_get_video_info(&video_info) ||
        video_info.base_width == 0U || video_info.base_height == 0U) {
        set_fixture_error(fixture, "OBS video output is unavailable");
        return;
    }
    fixture.base_width = video_info.base_width;
    fixture.base_height = video_info.base_height;

    fixture.scene_source = obs_frontend_get_current_scene();
    if (fixture.scene_source == nullptr) {
        set_fixture_error(fixture, "OBS has no current scene");
        return;
    }

    obs_scene_t *scene = obs_scene_from_source(fixture.scene_source);
    if (scene == nullptr) {
        set_fixture_error(fixture, "The current frontend source is not a scene");
        return;
    }

    obs_data_t *settings = obs_data_create();
    if (settings == nullptr) {
        set_fixture_error(fixture, "Display Capture settings could not be created");
        return;
    }
    obs_data_set_string(
        settings, "monitor_id", fixture.monitor_id.data());
    obs_data_set_int(settings, "method", 0);
    obs_data_set_bool(settings, "capture_cursor", false);
    obs_data_set_bool(settings, "force_sdr", true);

    fixture.display_source = obs_source_create_private(
        kDisplayCaptureSourceId,
        fixture.source_name.data(),
        settings);
    obs_data_release(settings);
    if (fixture.display_source == nullptr) {
        set_fixture_error(fixture, "OBS Display Capture source creation failed");
        return;
    }

    fixture.item = obs_scene_add(scene, fixture.display_source);
    if (fixture.item == nullptr) {
        set_fixture_error(fixture, "Display Capture could not be added to the current scene");
        return;
    }

    const vec2 origin{0.0F, 0.0F};
    obs_sceneitem_set_alignment(
        fixture.item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
    obs_sceneitem_set_pos(fixture.item, &origin);
    obs_sceneitem_set_visible(fixture.item, true);

    obs_source_inc_showing(fixture.display_source);
    fixture.manual_showing = true;
}

void fit_scene_fixture(void *opaque) noexcept
{
    auto &fixture = *static_cast<SceneFixture *>(opaque);
    if (fixture.item == nullptr || fixture.display_source == nullptr) {
        set_fixture_error(fixture, "Display Capture fixture disappeared before fitting");
        return;
    }

    const std::uint32_t source_width =
        obs_source_get_width(fixture.display_source);
    const std::uint32_t source_height =
        obs_source_get_height(fixture.display_source);
    if (source_width == 0U || source_height == 0U) {
        set_fixture_error(fixture, "Display Capture reported an empty frame size");
        return;
    }

    const vec2 origin{0.0F, 0.0F};
    const vec2 scale{
        static_cast<float>(fixture.base_width) /
            static_cast<float>(source_width),
        static_cast<float>(fixture.base_height) /
            static_cast<float>(source_height),
    };
    obs_sceneitem_set_alignment(
        fixture.item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
    obs_sceneitem_set_pos(fixture.item, &origin);
    obs_sceneitem_set_scale(fixture.item, &scale);
    obs_sceneitem_set_visible(fixture.item, true);
}

void cleanup_scene_fixture(void *opaque) noexcept
{
    auto &fixture = *static_cast<SceneFixture *>(opaque);

    if (fixture.item != nullptr) {
        obs_sceneitem_remove(fixture.item);
        fixture.item = nullptr;
    }
    if (fixture.manual_showing && fixture.display_source != nullptr) {
        obs_source_dec_showing(fixture.display_source);
        fixture.manual_showing = false;
    }
    if (fixture.display_source != nullptr) {
        obs_source_release(fixture.display_source);
        fixture.display_source = nullptr;
    }
    if (fixture.scene_source != nullptr) {
        obs_source_release(fixture.scene_source);
        fixture.scene_source = nullptr;
    }
}

bool wait_for_source_frame(
    SceneFixture &fixture, std::string &error) noexcept
{
    const ULONGLONG deadline =
        GetTickCount64() + kSourceReadyTimeoutMs;
    while (!stopping.load(std::memory_order_acquire) &&
           GetTickCount64() < deadline) {
        if (fixture.display_source != nullptr &&
            obs_source_get_width(fixture.display_source) > 0U &&
            obs_source_get_height(fixture.display_source) > 0U) {
            return true;
        }
        Sleep(50U);
    }

    error = stopping.load(std::memory_order_acquire)
                ? "Qualification was cancelled"
                : "Display Capture did not produce a frame before timeout";
    return false;
}

bool sample_main_texture(
    std::uint32_t width,
    std::uint32_t height,
    Pixel &pixel,
    std::string &error) noexcept
{
    if (width == 0U || height == 0U) {
        error = "OBS main texture has invalid dimensions";
        return false;
    }

    bool success = false;
    obs_enter_graphics();

    gs_texrender_t *render =
        gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *stage =
        gs_stagesurface_create(width, height, GS_RGBA);
    if (render == nullptr || stage == nullptr) {
        error = "OBS graphics staging resources could not be created";
    } else if (!gs_texrender_begin(render, width, height)) {
        error = "OBS main-texture render target could not begin";
    } else {
        vec4 clear_color{};
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0F, 0U);

        gs_viewport_push();
        gs_projection_push();
        gs_ortho(
            0.0F,
            static_cast<float>(width),
            0.0F,
            static_cast<float>(height),
            -100.0F,
            100.0F);
        gs_set_viewport(
            0,
            0,
            static_cast<int>(width),
            static_cast<int>(height));

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_render_main_texture();
        gs_blend_state_pop();

        gs_projection_pop();
        gs_viewport_pop();
        gs_texrender_end(render);

        gs_stage_texture(stage, gs_texrender_get_texture(render));

        std::uint8_t *data = nullptr;
        std::uint32_t linesize = 0U;
        const bool mapped =
            gs_stagesurface_map(stage, &data, &linesize);
        if (!mapped || data == nullptr || linesize < width * 4U) {
            error = "OBS main texture could not be mapped for reading";
        } else {
            const int center_x = static_cast<int>(width / 2U);
            const int center_y = static_cast<int>(height / 2U);
            constexpr int radius = 3;
            std::uint64_t first = 0U;
            std::uint64_t second = 0U;
            std::uint64_t third = 0U;
            std::uint64_t count = 0U;

            for (int y = std::max(0, center_y - radius);
                 y <= std::min(
                     static_cast<int>(height) - 1,
                     center_y + radius);
                 ++y) {
                const std::uint8_t *row =
                    data + static_cast<std::size_t>(y) * linesize;
                for (int x = std::max(0, center_x - radius);
                     x <= std::min(
                         static_cast<int>(width) - 1,
                         center_x + radius);
                     ++x) {
                    const std::uint8_t *sample =
                        row + static_cast<std::size_t>(x) * 4U;
                    first += sample[0];
                    second += sample[1];
                    third += sample[2];
                    ++count;
                }
            }

            if (count == 0U) {
                error = "OBS main-texture sample region was empty";
            } else {
                pixel.first = static_cast<int>(first / count);
                pixel.second = static_cast<int>(second / count);
                pixel.third = static_cast<int>(third / count);
                success = true;
            }
        }
        if (mapped) {
            gs_stagesurface_unmap(stage);
        }
    }

    if (stage != nullptr) {
        gs_stagesurface_destroy(stage);
    }
    if (render != nullptr) {
        gs_texrender_destroy(render);
    }
    obs_leave_graphics();
    return success;
}

int pixel_distance(const Pixel &left, const Pixel &right) noexcept
{
    return std::abs(left.first - right.first) +
           std::abs(left.second - right.second) +
           std::abs(left.third - right.third);
}

std::string pixel_text(const Pixel &pixel)
{
    return std::to_string(pixel.first) + "," +
           std::to_string(pixel.second) + "," +
           std::to_string(pixel.third);
}

void run_qualification() noexcept
{
    std::string failure;
    SceneFixture fixture;
    ProbeWindows probes;
    Pixel background;
    Pixel foreground;
    Pixel protected_foreground;
    Pixel hidden_foreground;
    bool fixture_created = false;

    try {
        if (!probes.create(failure)) {
            throw std::runtime_error(failure);
        }

        strncpy_s(
            fixture.monitor_id.data(),
            fixture.monitor_id.size(),
            probes.monitor_id().c_str(),
            _TRUNCATE);
        const std::string source_name =
            "ChatView OBS capture qualification " +
            std::to_string(GetCurrentProcessId());
        strncpy_s(
            fixture.source_name.data(),
            fixture.source_name.size(),
            source_name.c_str(),
            _TRUNCATE);

        Sleep(500U);
        obs_queue_task(
            OBS_TASK_UI, &create_scene_fixture, &fixture, true);
        fixture_created =
            fixture.scene_source != nullptr ||
            fixture.display_source != nullptr || fixture.item != nullptr;
        if (fixture.error.front() != '\0') {
            throw std::runtime_error(fixture.error.data());
        }

        if (!wait_for_source_frame(fixture, failure)) {
            throw std::runtime_error(failure);
        }
        obs_queue_task(OBS_TASK_UI, &fit_scene_fixture, &fixture, true);
        if (fixture.error.front() != '\0') {
            throw std::runtime_error(fixture.error.data());
        }
        Sleep(700U);

        if (!probes.show_background_only(failure) ||
            !sample_main_texture(
                fixture.base_width,
                fixture.base_height,
                background,
                failure)) {
            throw std::runtime_error(failure);
        }

        if (!probes.show_foreground_unprotected(failure) ||
            !sample_main_texture(
                fixture.base_width,
                fixture.base_height,
                foreground,
                failure)) {
            throw std::runtime_error(failure);
        }

        const int calibration_distance =
            pixel_distance(background, foreground);
        if (calibration_distance < kMinimumCalibrationDistance) {
            throw std::runtime_error(
                "OBS Display Capture did not distinguish the calibrated probe windows");
        }

        if (!probes.protect_foreground(failure) ||
            !sample_main_texture(
                fixture.base_width,
                fixture.base_height,
                protected_foreground,
                failure)) {
            throw std::runtime_error(failure);
        }

        if (!probes.hide_foreground(failure) ||
            !sample_main_texture(
                fixture.base_width,
                fixture.base_height,
                hidden_foreground,
                failure)) {
            throw std::runtime_error(failure);
        }

        const int background_to_hidden =
            pixel_distance(background, hidden_foreground);
        const int foreground_to_hidden =
            pixel_distance(foreground, hidden_foreground);
        const int hidden_limit = std::max(
            kHiddenDistanceFloor, calibration_distance / 5);
        if (background_to_hidden > hidden_limit ||
            background_to_hidden >= foreground_to_hidden) {
            throw std::runtime_error(
                "A hidden top-level window remained visible in the OBS main texture");
        }

        const int background_to_protected =
            pixel_distance(background, protected_foreground);
        const int foreground_to_protected =
            pixel_distance(foreground, protected_foreground);
        const bool affinity_honored =
            background_to_protected <=
                std::max(32, calibration_distance / 3) &&
            background_to_protected + 12 < foreground_to_protected;

        std::ostringstream report;
        report
            << "PASS\n"
            << "pipeline=monitor_capture>scene>main_texture\n"
            << "calibration_visible=1\n"
            << "hidden_window_excluded=1\n"
            << "affinity_honored=" << (affinity_honored ? 1 : 0) << '\n'
            << "capture_method=auto\n"
            << "canvas=" << fixture.base_width << 'x'
            << fixture.base_height << '\n'
            << "background_sample=" << pixel_text(background) << '\n'
            << "foreground_sample=" << pixel_text(foreground) << '\n'
            << "protected_sample="
            << pixel_text(protected_foreground) << '\n'
            << "hidden_sample=" << pixel_text(hidden_foreground) << '\n'
            << "calibration_distance=" << calibration_distance << '\n'
            << "background_to_protected="
            << background_to_protected << '\n'
            << "background_to_hidden=" << background_to_hidden << '\n';

        if (fixture_created) {
            obs_queue_task(
                OBS_TASK_UI, &cleanup_scene_fixture, &fixture, true);
            fixture_created = false;
        }
        probes.close();
        write_result_atomically(report.str());
        return;
    } catch (const std::exception &error) {
        failure = error.what();
    } catch (...) {
        failure = "Unknown capture-qualification failure";
    }

    if (fixture_created || fixture.scene_source != nullptr ||
        fixture.display_source != nullptr || fixture.item != nullptr) {
        obs_queue_task(
            OBS_TASK_UI, &cleanup_scene_fixture, &fixture, true);
    }
    probes.close();

    std::ostringstream report;
    report
        << "FAIL\n"
        << "pipeline=monitor_capture>scene>main_texture\n"
        << "reason=" << failure << '\n';
    write_result_atomically(report.str());
}

void on_frontend_event(obs_frontend_event event, void *) noexcept
{
    if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING ||
        result_path.empty() ||
        qualification_started.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    try {
        qualification_thread = std::thread(&run_qualification);
    } catch (const std::exception &error) {
        std::ostringstream report;
        report << "FAIL\npipeline=monitor_capture>scene>main_texture\n"
               << "reason=Qualification thread creation failed: "
               << error.what() << '\n';
        write_result_atomically(report.str());
    } catch (...) {
        write_result_atomically(
            "FAIL\npipeline=monitor_capture>scene>main_texture\n"
            "reason=Qualification thread creation failed\n");
    }
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
    return "ChatView OBS capture qualification";
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "CI-only Display Capture and OBS main-texture qualification module.";
}

bool obs_module_load(void)
{
    result_path = environment_value(kResultEnvironmentVariable);
    if (result_path.empty()) {
        return true;
    }

    stopping.store(false, std::memory_order_release);
    qualification_started.store(false, std::memory_order_release);
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    frontend_callback_registered = true;
    return true;
}

void obs_module_unload(void)
{
    if (frontend_callback_registered) {
        obs_frontend_remove_event_callback(on_frontend_event, nullptr);
        frontend_callback_registered = false;
    }

    stopping.store(true, std::memory_order_release);
    if (qualification_thread.joinable()) {
        qualification_thread.join();
    }
    result_path.clear();
    qualification_started.store(false, std::memory_order_release);
}
