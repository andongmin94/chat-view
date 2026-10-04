// SPDX-License-Identifier: GPL-2.0-or-later
// Real libobs scene/reference operations, no synthetic OBS API implementation.
// Does not exercise physical capture, the OBS frontend or the Windows desktop.
#include "obs-capture-scene.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
unsigned assertions = 0;
std::atomic<unsigned> destroyed_inputs{0};
void expect(bool value, const char *message)
{
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
void drain_fixture_destruction()
{
    // In pinned OBS 32.2.2, obs_wait_for_destroy_queue returns immediately
    // without BOTH the video and audio threads. This core-only fixture has
    // neither. Fence the actual destroy worker instead of sleeping/retrying.
    // The scene destructor can enqueue destruction of its child input behind
    // the first fence. Our fixture has exactly those two ownership levels.
    obs_queue_task(OBS_TASK_DESTROY, [](void *) {}, nullptr, true);
    obs_queue_task(OBS_TASK_DESTROY, [](void *) {}, nullptr, true);
}
struct Source {
    obs_source_t *value;
    ~Source() { obs_source_release(value); }
    Source(const Source &) = delete;
    Source &operator=(const Source &) = delete;
    explicit Source(obs_source_t *source) : value(source) {}
};
struct Scene {
    obs_scene_t *value = obs_scene_create_private("original scene");
    Scene()
    {
        if (!value) return;
        // This core-only test does not start a video mixer. Use libobs' loaded
        // custom scene dimensions rather than normalizing coordinates against
        // an unconfigured zero-sized canvas. No production scene is changed.
        obs_source_t *source = obs_scene_get_source(value);
        obs_data_t *data = obs_source_get_settings(source);
        obs_data_set_bool(data, "custom_size", true);
        obs_data_set_int(data, "cx", 960);
        obs_data_set_int(data, "cy", 540);
        obs_data_release(data);
        obs_source_load2(source);
    }
    ~Scene()
    {
        // Detach the fixture's canvas registration before releasing our ref.
        // Do not assume that every canvas holds a strong SCENE_REF reference.
        if (value) obs_canvas_scene_remove(value);
        obs_scene_release(value);
    }
};
std::size_t items(obs_scene_t *scene)
{
    std::size_t count = 0;
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *, void *opaque) {
        ++*static_cast<std::size_t *>(opaque); return true;
    }, &count);
    return count;
}
std::string settings(obs_source_t *source)
{
    obs_data_t *data = obs_source_get_settings(source);
    const char *json = obs_data_get_json(data);
    const std::string copy = json ? json : "";
    obs_data_release(data);
    return copy;
}
void preserved(obs_scene_t *scene, obs_sceneitem_t *original, obs_source_t *source,
               const std::string &before, float x = 71.0F)
{
    expect(items(scene) == 1, "only the original scene item remains");
    expect(obs_sceneitem_get_scene(original) == scene, "original item still belongs to its scene");
    expect(obs_sceneitem_get_source(original) == source, "original source identity preserved");
    vec2 pos{}, scale{};
    obs_sceneitem_get_pos(original, &pos); obs_sceneitem_get_scale(original, &scale);
    expect(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(scale.x) && std::isfinite(scale.y),
        "fixture transform is finite before and after cleanup");
    expect(pos.x == x && pos.y == 83.0F, "user position preserved, including concurrent edits");
    expect(scale.x == 0.5F && scale.y == 0.75F, "user scale preserved");
    expect(!obs_sceneitem_visible(original) && obs_sceneitem_locked(original), "user visibility and lock preserved");
    expect(settings(source) == before, "source settings are not overwritten by a test snapshot");
}
void run()
{
    obs_source_info info{};
    info.id = "chatview-lifecycle-test-input";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = [](void *) { return "Synthetic lifecycle input"; };
    info.create = [](obs_data_t *, obs_source_t *) -> void * { return new int(1); };
    info.destroy = [](void *data) { delete static_cast<int *>(data); ++destroyed_inputs; };
    info.get_width = [](void *) -> std::uint32_t { return 320; };
    info.get_height = [](void *) -> std::uint32_t { return 180; };
    obs_register_source(&info);
    Scene scene;
    expect(scene.value != nullptr, "real private scene created");
    expect(obs_source_get_width(obs_scene_get_source(scene.value)) == 960 &&
        obs_source_get_height(obs_scene_get_source(scene.value)) == 540, "fixture has real loaded scene dimensions");
    Source original(obs_source_create_private(info.id, "original source", nullptr));
    Source probe(obs_source_create_private(info.id, "temporary capture", nullptr));
    expect(original.value && probe.value, "real synthetic sources created");
    obs_sceneitem_t *item = obs_scene_add(scene.value, original.value);
    expect(item != nullptr, "original item added");
    const vec2 pos{71.0F, 83.0F}, scale{0.5F, 0.75F};
    obs_sceneitem_set_pos(item, &pos); obs_sceneitem_set_scale(item, &scale);
    obs_sceneitem_set_locked(item, true); obs_sceneitem_set_visible(item, false);
    obs_data_t *original_settings = obs_source_get_settings(original.value);
    obs_data_set_string(original_settings, "user-setting", "keep this exact value");
    obs_data_release(original_settings);
    const std::string before = settings(original.value);
    // Verify the fixture itself before any CaptureScene operation. A bad setup
    // must not be misreported as a restoration failure in the ownership helper.
    preserved(scene.value, item, original.value, before);
    auto owned_scene = [&] { return obs_source_get_ref(obs_scene_get_source(scene.value)); };

    { // Normal completion and idempotent cleanup.
        chatview::test::CaptureScene capture(owned_scene());
        expect(capture.attach(probe.value), "attach a retained capture item");
        expect(items(scene.value) == 2 && capture.source() == probe.value, "capture added only once");
        expect(!capture.attach(probe.value), "duplicate start does not add another item");
        expect(capture.fit(960, 540), "fit the temporary item only");
        capture.close(); capture.close();
        expect(!capture.item() && !capture.source(), "cleanup drops retained resources");
        expect(!capture.attach(probe.value), "closed fixture cannot resume");
    }
    preserved(scene.value, item, original.value, before);

    { // Partial creation/failure before attachment.
        chatview::test::CaptureScene capture(owned_scene());
        expect(!capture.attach(nullptr), "missing input cannot alter scene");
        expect(!capture.fit(960, 540), "incomplete fixture cannot be fitted");
    }
    preserved(scene.value, item, original.value, before);

    try { // Failure after attachment unwinds through the same cleanup.
        chatview::test::CaptureScene capture(owned_scene());
        expect(capture.attach(probe.value), "attach before simulated failure");
        expect(!capture.fit(0, 540), "invalid output dimensions rejected");
        throw std::runtime_error("synthetic capture failure");
    } catch (const std::runtime_error &error) {
        expect(std::string(error.what()) == "synthetic capture failure", "unwind the intended failure");
    }
    preserved(scene.value, item, original.value, before);

    { // Frontend removes our item before worker cleanup: the retained pointer
      // remains valid, but fitting it again is forbidden.
        chatview::test::CaptureScene capture(owned_scene());
        expect(capture.attach(probe.value), "attach before external removal");
        obs_sceneitem_remove(capture.item());
        expect(!capture.fit(960, 540), "externally removed capture is not resurrected");
        capture.close();
    }
    preserved(scene.value, item, original.value, before);

    { // A user edit during the test survives; no full-scene rollback.
        chatview::test::CaptureScene capture(owned_scene());
        expect(capture.attach(probe.value), "attach before user edit");
        const vec2 changed{119.0F, 83.0F}; obs_sceneitem_set_pos(item, &changed);
    }
    preserved(scene.value, item, original.value, before, 119.0F);
    obs_sceneitem_set_pos(item, &pos);

    for (int cancel_at = 0; cancel_at != 3; ++cancel_at) {
        // The owner blocks in join; no frontend/UI queue is pumped. This is the
        // actual ownership helper used by the qualification worker.
        obs_source_t *retained = owned_scene();
        std::atomic<bool> attached{false}, cancel{false}, succeeded{true};
        std::thread worker([&, retained, cancel_at] {
            chatview::test::CaptureScene capture(retained);
            if (cancel_at == 0) return;
            if (!capture.attach(probe.value)) { succeeded = false; attached = true; return; }
            if (cancel_at == 2 && !capture.fit(960, 540)) succeeded = false;
            attached = true;
            while (!cancel.load()) std::this_thread::yield();
            capture.close();
        });
        if (cancel_at != 0) while (!attached.load()) std::this_thread::yield();
        cancel = true; worker.join();
        expect(succeeded.load(), "worker cleanup completes without frontend tasks");
        preserved(scene.value, item, original.value, before);
    }
    { // No valid frontend scene means no test source is attached.
        chatview::test::CaptureScene capture(nullptr);
        expect(!capture.attach(probe.value), "missing original scene rejected");
    }
    preserved(scene.value, item, original.value, before);
    expect(!obs_source_showing(probe.value), "manual showing count is balanced after every cleanup");
}
}
int main()
{
    if (!obs_startup("en-US", nullptr, nullptr)) return 1;
    int result = 0;
    try {
        run();
        drain_fixture_destruction();
        std::size_t remaining = 0;
        obs_enum_all_sources([](void *value, obs_source_t *source) {
            ++*static_cast<std::size_t *>(value);
            std::cerr << "Remaining fixture source: " << obs_source_get_name(source) << '\n';
            return true;
        }, &remaining);
        expect(remaining == 0, "scene and all source references drained before OBS shutdown");
        expect(destroyed_inputs.load() == 2, "both real input destructors completed before OBS shutdown");
        std::cout << "Capture scene lifecycle: " << assertions << " assertions passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    drain_fixture_destruction();
    obs_shutdown();
    return result;
}
