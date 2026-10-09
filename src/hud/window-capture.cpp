// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud/window-capture.hpp"
#include "hud/capture-worker-wait.hpp"
#include "hud/video-layout.hpp"
#include "hud/video-frame-time.hpp"
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>
#include <utility>

namespace chatview {
namespace {
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using winrt::check_hresult;
void require(bool value) { if (!value) winrt::throw_hresult(E_INVALIDARG); }
struct Event {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (value) CloseHandle(value); }
    Event() { require(value != nullptr); }
    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;
};
bool source_alive(HWND source, DWORD process, DWORD thread) noexcept
{
    DWORD pid = 0, cloaked = 0;
    return IsWindow(source) && GetWindowThreadProcessId(source, &pid) == thread && pid == process &&
        IsWindowVisible(source) && !IsIconic(source) &&
        SUCCEEDED(DwmGetWindowAttribute(source, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && !cloaked;
}
// Use the Windows GPU libraries already shipped with the OS, not a custom
// scaler/encoder. Only this worker accesses the D3D immediate/D2D contexts.
class VideoPresenter final {
public:
    winrt::com_ptr<ID3D11Device> device;
    Direct3D11::IDirect3DDevice capture_device{nullptr};
    explicit VideoPresenter(HWND output) : output_(output)
    {
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            device.put(), nullptr, immediate_.put()));
        auto dxgi = device.as<IDXGIDevice>();
        winrt::com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        capture_device = inspectable.as<Direct3D11::IDirect3DDevice>();
        winrt::com_ptr<IDXGIAdapter> adapter;
        check_hresult(dxgi->GetAdapter(adapter.put()));
        winrt::com_ptr<IDXGIFactory2> factory;
        check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        RECT rect{}; require(GetClientRect(output_, &rect) != FALSE);
        require(video_size(rect.right, rect.bottom));
        description.Width = static_cast<UINT>(rect.right); description.Height = static_cast<UINT>(rect.bottom);
        check_hresult(factory->CreateSwapChainForHwnd(device.get(), output_, &description, nullptr, nullptr, swap_.put()));
        check_hresult(factory->MakeWindowAssociation(output_, DXGI_MWA_NO_ALT_ENTER));
        winrt::com_ptr<ID2D1Factory1> d2d_factory;
        D2D1_FACTORY_OPTIONS options{};
        check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, d2d_factory.put_void()));
        winrt::com_ptr<ID2D1Device> d2d_device;
        check_hresult(d2d_factory->CreateDevice(dxgi.get(), d2d_device.put()));
        check_hresult(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context_.put()));
        target(rect.right, rect.bottom);
        clear();
    }
    void clear()
    {
        if (!context_ || !target_) return;
        context_->BeginDraw(); context_->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        check_hresult(context_->EndDraw()); check_hresult(swap_->Present(1, 0));
    }
    void show(ID3D11Texture2D *texture, SizeInt32 size)
    {
        require(video_size(size.Width, size.Height));
        D3D11_TEXTURE2D_DESC captured{}; texture->GetDesc(&captured);
        require(captured.Width >= static_cast<UINT>(size.Width) && captured.Height >= static_cast<UINT>(size.Height) &&
            captured.Format == DXGI_FORMAT_B8G8R8A8_UNORM && captured.SampleDesc.Count == 1);
        if (!copy_ || source_width_ != size.Width || source_height_ != size.Height) {
            bitmap_ = nullptr; copy_ = nullptr;
            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(size.Width); description.Height = static_cast<UINT>(size.Height);
            description.MipLevels = 1; description.ArraySize = 1; description.SampleDesc.Count = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            check_hresult(device->CreateTexture2D(&description, nullptr, copy_.put()));
            auto surface = copy_.as<IDXGISurface>();
            D2D1_BITMAP_PROPERTIES1 properties{};
            properties.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
            properties.dpiX = properties.dpiY = 96;
            check_hresult(context_->CreateBitmapFromDxgiSurface(surface.get(), &properties, bitmap_.put()));
            source_width_ = size.Width; source_height_ = size.Height;
        }
        // ContentSize, not pool allocation size: resized frame padding is undefined.
        const D3D11_BOX box{0, 0, 0, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1};
        immediate_->CopySubresourceRegion(copy_.get(), 0, 0, 0, 0, texture, 0, &box);
        RECT rect{}; require(GetClientRect(output_, &rect) != FALSE && video_size(rect.right, rect.bottom));
        if (rect.right != width_ || rect.bottom != height_) target(rect.right, rect.bottom);
        const auto fit = video_fit(size.Width, size.Height, width_, height_);
        const D2D1_RECT_F destination{static_cast<float>(fit.left), static_cast<float>(fit.top),
            static_cast<float>(fit.right), static_cast<float>(fit.bottom)};
        context_->BeginDraw(); context_->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        context_->DrawBitmap(bitmap_.get(), &destination, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
        check_hresult(context_->EndDraw()); check_hresult(swap_->Present(1, 0));
    }
private:
    void target(int width, int height)
    {
        context_->SetTarget(nullptr); target_ = nullptr;
        if (width_ != 0) check_hresult(swap_->ResizeBuffers(2, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        winrt::com_ptr<IDXGISurface> surface;
        check_hresult(swap_->GetBuffer(0, __uuidof(IDXGISurface), surface.put_void()));
        D2D1_BITMAP_PROPERTIES1 properties{};
        properties.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
        properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        properties.dpiX = properties.dpiY = 96;
        check_hresult(context_->CreateBitmapFromDxgiSurface(surface.get(), &properties, target_.put()));
        context_->SetTarget(target_.get()); width_ = width; height_ = height;
    }
    HWND output_;
    int width_ = 0, height_ = 0, source_width_ = 0, source_height_ = 0;
    winrt::com_ptr<ID3D11DeviceContext> immediate_;
    winrt::com_ptr<IDXGISwapChain1> swap_;
    winrt::com_ptr<ID2D1DeviceContext> context_;
    winrt::com_ptr<ID3D11Texture2D> copy_;
    winrt::com_ptr<ID2D1Bitmap1> bitmap_, target_;
};
struct CaptureSession {
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    Direct3D11CaptureFramePool::FrameArrived_revoker frame;
    GraphicsCaptureItem::Closed_revoker closed;
    ~CaptureSession()
    {
        frame.revoke(); closed.revoke();
        try { if (session) session.Close(); } catch (...) {}
        try { if (pool) pool.Close(); } catch (...) {}
    }
};
}
struct WindowCapture::State {
    Event wake;
    std::atomic<bool> cancel{false}, closed{false}, finished{false};
    std::atomic<WindowCaptureStatus> status{WindowCaptureStatus::Starting};
    std::atomic<unsigned> width{0}, height{0};
    std::atomic<std::uint64_t> frames{0}, content_at_ms{0};
    std::atomic<unsigned> received_width{0}, received_height{0}, surface_width{0}, surface_height{0};
    std::atomic<std::uint64_t> recreates{0}, clipped_frames{0};
    std::atomic<std::int32_t> failure_hresult{0};
    std::atomic<unsigned> pool_width{0}, pool_height{0};
};
WindowCapture::~WindowCapture() { close(); }
void WindowCapture::close() noexcept
{
    if (closing_) return;
    closing_ = true;
    stop();
    join_capture_worker(worker_);
    closing_ = false;
}
bool WindowCapture::start(HWND source, HWND output) noexcept
{
    try {
        if (closing_ || running()) return false;
        if (worker_.joinable()) worker_.join();
        DWORD process = 0, output_process = 0;
        const DWORD thread = GetWindowThreadProcessId(source, &process);
        (void)GetWindowThreadProcessId(output, &output_process);
        if (source == GetDesktopWindow() || source == GetShellWindow() || !thread || process == GetCurrentProcessId() || output_process != GetCurrentProcessId() ||
            !source_alive(source, process, thread) || GetAncestor(source, GA_ROOT) != source) return false;
        auto state = std::make_shared<State>();
        worker_ = std::thread(&WindowCapture::run, state, source, output, process, thread); state_ = std::move(state);
        return true;
    } catch (...) { return false; }
}
void WindowCapture::stop() noexcept
{
    if (state_) { state_->cancel.store(true); state_->content_at_ms.store(0); SetEvent(state_->wake.value); }
}
bool WindowCapture::running() const noexcept { return state_ && !state_->finished.load(); }
WindowCaptureSnapshot WindowCapture::snapshot() const noexcept
{
    if (!state_) return {};
    return {state_->status.load(), state_->width.load(), state_->height.load(), state_->frames.load(), state_->content_at_ms.load(),
        state_->received_width.load(), state_->received_height.load(), state_->surface_width.load(), state_->surface_height.load(),
        state_->recreates.load(), state_->clipped_frames.load(), state_->failure_hresult.load(),
        state_->pool_width.load(), state_->pool_height.load()};
}
void WindowCapture::run(std::shared_ptr<State> state, HWND source, HWND output, DWORD process, DWORD thread) noexcept
{
    WindowCaptureStatus final_status = WindowCaptureStatus::Stopped;
    bool apartment = false;
    std::unique_ptr<VideoPresenter> presenter;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true;
        require(GraphicsCaptureSession::IsSupported());
        presenter = std::make_unique<VideoPresenter>(output);
        CaptureSession capture;
        const auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        check_hresult(interop->CreateForWindow(source, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(capture.item)));
        auto size = capture.item.Size(); require(video_size(size.Width, size.Height));
        capture.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(presenter->capture_device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        state->pool_width.store(static_cast<unsigned>(size.Width));
        state->pool_height.store(static_cast<unsigned>(size.Height));
        capture.session = capture.pool.CreateCaptureSession(capture.item);
        // Keep the system capture border. Never request borderless access.
        capture.frame = capture.pool.FrameArrived(winrt::auto_revoke, [state](auto const &, auto const &) {
            SetEvent(state->wake.value);
        });
        capture.closed = capture.item.Closed(winrt::auto_revoke, [state](auto const &, auto const &) {
            state->closed.store(true); SetEvent(state->wake.value);
        });
        capture.session.StartCapture();
        ULONGLONG last = GetTickCount64(); bool painted = false;
        while (!state->cancel.load()) {
            if (state->closed.load() || !source_alive(source, process, thread)) {
                final_status = WindowCaptureStatus::SourceLost; break;
            }
            if (!IsWindow(output)) break;
            const DWORD wait = WaitForSingleObject(state->wake.value, 50);
            require(wait == WAIT_OBJECT_0 || wait == WAIT_TIMEOUT);
            if (state->cancel.load()) break;
            auto frame = capture.pool.TryGetNextFrame();
            if (!frame) {
                if (GetTickCount64() - last >= kVideoFrameLifetimeMs) {
                    state->content_at_ms.store(0);
                    if (painted) { presenter->clear(); painted = false; }
                    state->status.store(WindowCaptureStatus::Waiting);
                }
                continue;
            }
            // At most two retained buffers. Prefer the latest without a growing queue.
            if (auto newer = capture.pool.TryGetNextFrame()) { frame.Close(); frame = std::move(newer); }
            const auto content = frame.ContentSize();
            require(video_size(content.Width, content.Height));
            if (state->closed.load() || !source_alive(source, process, thread) || state->cancel.load()) { frame.Close(); continue; }
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            winrt::com_ptr<ID3D11Texture2D> texture;
            check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
            D3D11_TEXTURE2D_DESC surface{}; texture->GetDesc(&surface);
            state->received_width.store(static_cast<unsigned>(content.Width));
            state->received_height.store(static_cast<unsigned>(content.Height));
            state->surface_width.store(surface.Width); state->surface_height.store(surface.Height);
            require(surface.Format == DXGI_FORMAT_B8G8R8A8_UNORM && surface.SampleDesc.Count == 1);
            const auto action = video_frame_action(content.Width, content.Height, surface.Width, surface.Height, size.Width, size.Height);
            require(action != VideoFrameAction::Invalid);
            const bool resize = action == VideoFrameAction::PresentAndResize || action == VideoFrameAction::ResizeOnly;
            if (action != VideoFrameAction::Present || content.Width != static_cast<int>(state->width.load()) ||
                content.Height != static_cast<int>(state->height.load())) {
                // The owner must mask stale geometry even if a GPU call stalls.
                state->content_at_ms.store(0); state->status.store(WindowCaptureStatus::Waiting);
            }
            const auto release_frame = [&] { texture = nullptr; access = nullptr; frame.Close(); };
            const auto recreate = [&] {
                // No checked-out frame or surface may outlive Recreate. It also
                // discards pending frames, so consume a complete one FIRST.
                // Keep capacity across shrink/return transitions. Recreate only
                // for growth beyond that capacity, never for a late old surface.
                const SizeInt32 capacity{std::max(size.Width, content.Width), std::max(size.Height, content.Height)};
                capture.pool.Recreate(presenter->capture_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, capacity);
                size = capacity;
                state->pool_width.store(static_cast<unsigned>(size.Width));
                state->pool_height.store(static_cast<unsigned>(size.Height));
                state->recreates.fetch_add(1);
            };
            if (action == VideoFrameAction::ResizeOnly || action == VideoFrameAction::WaitForSurface) {
                // A growing ContentSize can exceed the actual old allocation,
                // even after the requested pool size has caught up. Never copy
                // clipped pixels or turn this normal transition into Failed.
                state->clipped_frames.fetch_add(1);
                release_frame(); presenter->clear(); painted = false;
                if (resize) {
                    // A static window may paint only once at the larger size.
                    // Retire the old session AND pool before opening a fresh
                    // pool/session on the same capture item. Recreate on the old
                    // pool after closing its session failed with E_UNEXPECTED.
                    capture.frame.revoke();
                    capture.session.Close();
                    capture.session = nullptr;
                    capture.pool.Close();
                    const SizeInt32 capacity{std::max(size.Width, content.Width), std::max(size.Height, content.Height)};
                    capture.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(presenter->capture_device,
                        DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, capacity);
                    capture.frame = capture.pool.FrameArrived(winrt::auto_revoke, [state](auto const &, auto const &) {
                        SetEvent(state->wake.value);
                    });
                    size = capacity;
                    state->pool_width.store(static_cast<unsigned>(size.Width));
                    state->pool_height.store(static_cast<unsigned>(size.Height));
                    state->recreates.fetch_add(1);
                    if (!state->cancel.load() && !state->closed.load() && source_alive(source, process, thread)) {
                        capture.session = capture.pool.CreateCaptureSession(capture.item);
                        capture.session.StartCapture();
                    }
                }
                continue;
            }
            // Snapshot the local clock before QPC to conservatively map content
            // age, never the completion time of a potentially blocking Present.
            const auto observed = GetTickCount64();
            LARGE_INTEGER counter{}, frequency{};
            require(QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
            const double age = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart) -
                static_cast<double>(frame.SystemRelativeTime().count()) / 10000000.0;
            const auto content_at = video_frame_time(observed, age);
            if (!video_frame_fresh(GetTickCount64(), content_at)) {
                state->content_at_ms.store(0);
                release_frame(); presenter->clear(); painted = false; state->status.store(WindowCaptureStatus::Waiting);
                if (resize) recreate();
                continue;
            }
            // Exact ContentSize copy excludes undefined pool padding. Preserve
            // this fresh complete transition frame (notably on static shrink)
            // before Recreate discards any remaining pending frames.
            presenter->show(texture.get(), content);
            release_frame();
            if (resize) recreate();
            if (state->cancel.load() || state->closed.load() || !source_alive(source, process, thread) ||
                !video_frame_fresh(GetTickCount64(), content_at)) {
                state->content_at_ms.store(0);
                presenter->clear(); painted = false; state->status.store(WindowCaptureStatus::Waiting); continue;
            }
            painted = true; last = content_at;
            state->width.store(static_cast<unsigned>(content.Width)); state->height.store(static_cast<unsigned>(content.Height));
            state->content_at_ms.store(content_at);
            state->frames.fetch_add(1); state->status.store(WindowCaptureStatus::Capturing);
        }
    } catch (...) {
        state->failure_hresult.store(static_cast<std::int32_t>(winrt::to_hresult()));
        final_status = WindowCaptureStatus::Failed;
    }
    // Invalidate freshness BEFORE GPU teardown; the owner can independently
    // cover an old frame even when clear/Present or driver cleanup is delayed.
    state->content_at_ms.store(0);
    // Keep the output HWND black on stop/failure; only its owner may release it.
    try { if (presenter) presenter->clear(); } catch (...) {
        if (!state->failure_hresult.load()) state->failure_hresult.store(static_cast<std::int32_t>(winrt::to_hresult()));
        final_status = WindowCaptureStatus::Failed;
    }
    presenter.reset();
    if (apartment) winrt::uninit_apartment();
    state->status.store(final_status); state->finished.store(true);
}
}
