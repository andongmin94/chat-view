// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace chatview {

enum class HudLaunchMode { ObsControlled, Companion };

struct HudLaunchOptions {
    HudLaunchMode mode = HudLaunchMode::ObsControlled;
    std::wstring mapping_name;
    std::wstring event_name;
    std::wstring ready_event_name;
    std::uint32_t parent_process_id = 0U;
};

// Arguments exclude argv[0]. Companion is an explicit, exclusive mode: an
// invalid/missing OBS controller must never silently produce an unmanaged HUD.
[[nodiscard]] inline std::optional<HudLaunchOptions> parse_hud_launch_options(
    std::span<const std::wstring_view> arguments)
{
    HudLaunchOptions result;
    if (arguments.size() == 1U && arguments.front() == L"--companion") {
        result.mode = HudLaunchMode::Companion;
        return result;
    }
    if (arguments.empty() || arguments.size() % 2U != 0U) return std::nullopt;

    for (std::size_t index = 0U; index < arguments.size(); index += 2U) {
        const auto name = arguments[index];
        const auto value = arguments[index + 1U];
        if (value.empty() || value.starts_with(L"--") ||
            value.find(L'\0') != std::wstring_view::npos) return std::nullopt;
        if (name == L"--parent") {
            if (result.parent_process_id != 0U) return std::nullopt;
            std::uint32_t pid = 0U;
            for (const wchar_t digit : value) {
                if (digit < L'0' || digit > L'9') return std::nullopt;
                const auto number = static_cast<std::uint32_t>(digit - L'0');
                if (pid > (std::numeric_limits<std::uint32_t>::max() - number) / 10U)
                    return std::nullopt;
                pid = pid * 10U + number;
            }
            if (pid == 0U) return std::nullopt;
            result.parent_process_id = pid;
        } else {
            std::wstring *destination = nullptr;
            if (name == L"--mapping") destination = &result.mapping_name;
            else if (name == L"--event") destination = &result.event_name;
            else if (name == L"--ready-event") destination = &result.ready_event_name;
            if (destination == nullptr || !destination->empty()) return std::nullopt;
            destination->assign(value);
        }
    }
    if (result.mapping_name.empty() || result.event_name.empty() ||
        result.parent_process_id == 0U) return std::nullopt;
    return result;
}

} // namespace chatview
