// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs.h>
#include <graphics/vec2.h>
#include <cstdint>
#include <utility>

namespace chatview::test {
// CI-only scene ownership. The caller transfers its retained scene source on
// the frontend thread; worker operations below use only libobs, never UI tasks.
// Existing scene items/settings are not replaced with an old saved snapshot.
class CaptureScene final {
public:
    explicit CaptureScene(obs_source_t *owned_scene) noexcept : scene_(owned_scene) {}
    ~CaptureScene() { close(); }
    CaptureScene(const CaptureScene &) = delete;
    CaptureScene &operator=(const CaptureScene &) = delete;

    // Borrow the source, retaining our own reference only after validation.
    bool attach(obs_source_t *source) noexcept
    {
        if (!scene_ || item_ || source_ || !source || obs_source_removed(scene_)) return false;
        obs_scene_t *scene = obs_scene_from_source(scene_);
        if (!scene) return false;
        source_ = obs_source_get_ref(source);
        if (!source_) return false;
        obs_scene_atomic_update(scene, [](void *opaque, obs_scene_t *parent) {
            auto &self = *static_cast<CaptureScene *>(opaque);
            self.item_ = obs_scene_add(parent, self.source_);
            // obs_scene_add returns a borrowed item. Retain it while the scene
            // lock is still held, including when the frontend later removes it.
            if (self.item_) obs_sceneitem_addref(self.item_);
        }, this);
        if (!item_) {
            obs_source_release(std::exchange(source_, nullptr));
            return false;
        }
        obs_source_inc_showing(source_);
        showing_ = true;
        return true;
    }

    bool fit(std::uint32_t width, std::uint32_t height) noexcept
    {
        if (!scene_ || !source_ || !item_ || !width || !height || obs_source_removed(scene_)) return false;
        const auto source_width = obs_source_get_width(source_);
        const auto source_height = obs_source_get_height(source_);
        if (!source_width || !source_height) return false;
        struct Update { CaptureScene *self; vec2 scale; bool applied = false; } update{
            this, {static_cast<float>(width) / static_cast<float>(source_width),
                   static_cast<float>(height) / static_cast<float>(source_height)}};
        obs_scene_atomic_update(obs_scene_from_source(scene_), [](void *opaque, obs_scene_t *parent) {
            auto &value = *static_cast<Update *>(opaque);
            if (obs_sceneitem_get_scene(value.self->item_) != parent) return;
            const vec2 origin{0.0F, 0.0F};
            obs_sceneitem_defer_update_begin(value.self->item_);
            obs_sceneitem_set_alignment(value.self->item_, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
            obs_sceneitem_set_pos(value.self->item_, &origin);
            obs_sceneitem_set_scale(value.self->item_, &value.scale);
            obs_sceneitem_defer_update_end(value.self->item_);
            obs_sceneitem_set_visible(value.self->item_, true);
            value.applied = true;
        }, &update);
        return update.applied;
    }

    obs_source_t *source() const noexcept { return source_; }
    obs_sceneitem_t *item() const noexcept { return item_; }

    void close() noexcept
    {
        // Remove only our retained item. Removal may already have happened due
        // to frontend close/scene collection cleanup; our reference is valid.
        if (auto *item = std::exchange(item_, nullptr)) {
            obs_sceneitem_remove(item);
            obs_sceneitem_release(item);
        }
        if (showing_) {
            obs_source_dec_showing(source_);
            showing_ = false;
        }
        obs_source_release(std::exchange(source_, nullptr));
        obs_source_release(std::exchange(scene_, nullptr));
    }

private:
    obs_source_t *scene_ = nullptr;
    obs_source_t *source_ = nullptr;
    obs_sceneitem_t *item_ = nullptr;
    bool showing_ = false;
};
}
