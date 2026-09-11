from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[1]
plugin_path = root / "tests" / "obs-capture-qualification-plugin.cpp"
test_path = root / "tests" / "obs-capture-qualification-test.cpp"
cmake_path = root / "CMakeLists.txt"

plugin = plugin_path.read_text(encoding="utf-8")
plugin = replace_once(
    plugin,
    """constexpr DWORD kSourceReadyTimeoutMs = 12000U;\nconstexpr DWORD kProbeSettleMs = 450U;\nconstexpr int kMinimumCalibrationDistance = 72;\nconstexpr int kHiddenDistanceFloor = 24;\n""",
    """constexpr DWORD kSourceReadyTimeoutMs = 12000U;\nconstexpr DWORD kProbeSettleMs = 450U;\nconstexpr DWORD kTransitionTimeoutMs = 6000U;\nconstexpr DWORD kTransitionSampleIntervalMs = 100U;\nconstexpr unsigned int kRequiredConsecutiveSamples = 3U;\nconstexpr int kMinimumCalibrationDistance = 72;\nconstexpr int kHiddenDistanceFloor = 24;\n""",
    "qualification timing constants",
)

plugin = replace_once(
    plugin,
    """std::string pixel_text(const Pixel &pixel)\n{\n    return std::to_string(pixel.first) + \",\" +\n           std::to_string(pixel.second) + \",\" +\n           std::to_string(pixel.third);\n}\n\nvoid run_qualification() noexcept\n""",
    """std::string pixel_text(const Pixel &pixel)\n{\n    return std::to_string(pixel.first) + \",\" +\n           std::to_string(pixel.second) + \",\" +\n           std::to_string(pixel.third);\n}\n\nbool wait_for_distinct_sample(\n    std::uint32_t width,\n    std::uint32_t height,\n    const Pixel &background,\n    Pixel &sample,\n    std::string &error) noexcept\n{\n    const ULONGLONG deadline = GetTickCount64() + kTransitionTimeoutMs;\n    unsigned int consecutive = 0U;\n\n    while (!stopping.load(std::memory_order_acquire) &&\n           GetTickCount64() < deadline) {\n        Pixel candidate;\n        if (!sample_main_texture(width, height, candidate, error)) {\n            return false;\n        }\n        sample = candidate;\n\n        if (pixel_distance(background, candidate) >=\n            kMinimumCalibrationDistance) {\n            ++consecutive;\n            if (consecutive >= kRequiredConsecutiveSamples) {\n                return true;\n            }\n        } else {\n            consecutive = 0U;\n        }\n        Sleep(kTransitionSampleIntervalMs);\n    }\n\n    error = stopping.load(std::memory_order_acquire)\n                ? \"Qualification was cancelled\"\n                : \"OBS Display Capture did not distinguish the calibrated probe windows\";\n    return false;\n}\n\nbool wait_for_background_sample(\n    std::uint32_t width,\n    std::uint32_t height,\n    const Pixel &background,\n    const Pixel &foreground,\n    int background_limit,\n    const char *timeout_message,\n    Pixel &sample,\n    std::string &error) noexcept\n{\n    const ULONGLONG deadline = GetTickCount64() + kTransitionTimeoutMs;\n    unsigned int consecutive = 0U;\n\n    while (!stopping.load(std::memory_order_acquire) &&\n           GetTickCount64() < deadline) {\n        Pixel candidate;\n        if (!sample_main_texture(width, height, candidate, error)) {\n            return false;\n        }\n        sample = candidate;\n\n        const int background_distance =\n            pixel_distance(background, candidate);\n        const int foreground_distance =\n            pixel_distance(foreground, candidate);\n        if (background_distance <= background_limit &&\n            background_distance + 12 < foreground_distance) {\n            ++consecutive;\n            if (consecutive >= kRequiredConsecutiveSamples) {\n                return true;\n            }\n        } else {\n            consecutive = 0U;\n        }\n        Sleep(kTransitionSampleIntervalMs);\n    }\n\n    error = stopping.load(std::memory_order_acquire)\n                ? \"Qualification was cancelled\"\n                : timeout_message;\n    return false;\n}\n\nvoid run_qualification() noexcept\n""",
    "transition sampling helpers",
)

plugin = replace_once(
    plugin,
    """        if (!probes.show_foreground_unprotected(failure) ||\n            !sample_main_texture(\n                fixture.base_width,\n                fixture.base_height,\n                foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        const int calibration_distance =\n            pixel_distance(background, foreground);\n        if (calibration_distance < kMinimumCalibrationDistance) {\n            throw std::runtime_error(\n                \"OBS Display Capture did not distinguish the calibrated probe windows\");\n        }\n\n        if (!probes.protect_foreground(failure) ||\n            !sample_main_texture(\n                fixture.base_width,\n                fixture.base_height,\n                protected_foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        if (!probes.hide_foreground(failure) ||\n            !sample_main_texture(\n                fixture.base_width,\n                fixture.base_height,\n                hidden_foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        const int background_to_hidden =\n            pixel_distance(background, hidden_foreground);\n        const int foreground_to_hidden =\n            pixel_distance(foreground, hidden_foreground);\n        const int hidden_limit = std::max(\n            kHiddenDistanceFloor, calibration_distance / 5);\n""",
    """        if (!probes.show_foreground_unprotected(failure) ||\n            !wait_for_distinct_sample(\n                fixture.base_width,\n                fixture.base_height,\n                background,\n                foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        const int calibration_distance =\n            pixel_distance(background, foreground);\n        const int protected_limit = std::max(\n            32, calibration_distance / 3);\n        if (!probes.protect_foreground(failure) ||\n            !wait_for_background_sample(\n                fixture.base_width,\n                fixture.base_height,\n                background,\n                foreground,\n                protected_limit,\n                \"WDA_EXCLUDEFROMCAPTURE remained visible in the OBS main texture\",\n                protected_foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        const int hidden_limit = std::max(\n            kHiddenDistanceFloor, calibration_distance / 5);\n        if (!probes.hide_foreground(failure) ||\n            !wait_for_background_sample(\n                fixture.base_width,\n                fixture.base_height,\n                background,\n                foreground,\n                hidden_limit,\n                \"A hidden top-level window remained visible in the OBS main texture\",\n                hidden_foreground,\n                failure)) {\n            throw std::runtime_error(failure);\n        }\n\n        const int background_to_hidden =\n            pixel_distance(background, hidden_foreground);\n        const int foreground_to_hidden =\n            pixel_distance(foreground, hidden_foreground);\n""",
    "phase sampling sequence",
)

plugin = replace_once(
    plugin,
    """        const bool affinity_honored =\n            background_to_protected <=\n                std::max(32, calibration_distance / 3) &&\n            background_to_protected + 12 < foreground_to_protected;\n\n        std::ostringstream report;\n""",
    """        const bool affinity_honored =\n            background_to_protected <= protected_limit &&\n            background_to_protected + 12 < foreground_to_protected;\n        if (!affinity_honored) {\n            throw std::runtime_error(\n                \"WDA_EXCLUDEFROMCAPTURE remained visible in the OBS main texture\");\n        }\n\n        std::ostringstream report;\n""",
    "strict capture-affinity verdict",
)
plugin_path.write_text(plugin, encoding="utf-8")

test = test_path.read_text(encoding="utf-8")
test = replace_once(
    test,
    """        has_report_line(outcome.report, \"calibration_visible=1\") &&\n        has_report_line(outcome.report, \"hidden_window_excluded=1\");\n""",
    """        has_report_line(outcome.report, \"calibration_visible=1\") &&\n        has_report_line(outcome.report, \"hidden_window_excluded=1\") &&\n        has_report_line(outcome.report, \"affinity_honored=1\");\n""",
    "strict qualification report verdict",
)
test_path.write_text(test, encoding="utf-8")

cmake = cmake_path.read_text(encoding="utf-8")
cmake = replace_once(
    cmake,
    "project(chat-view-obs VERSION 0.3.1 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.3.2 LANGUAGES CXX)",
    "project version",
)
cmake_path.write_text(cmake, encoding="utf-8")
