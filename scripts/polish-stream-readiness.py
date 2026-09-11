from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


replace_once(
    "src/config/main.cpp",
    '''            refresh_stream_readiness(false, false, {}, false, {});
            return;
        }

        const bool status_changed =''',
    '''            refresh_stream_readiness(true, false, {}, false, {});
            return;
        }

        const bool status_changed =''',
)

replace_once(
    "src/common/control-status.hpp",
    '''    if (visible && !running) {
        return false;
    }
    return running == (snapshot.hud_process_id != 0U);
''',
    '''    if (visible && !running) {
        return false;
    }
    const bool scene_graph_ready = has_control_status_flag(
        snapshot, ControlStatusSceneGraphReady);
    const bool display_capture_active = has_control_status_flag(
        snapshot, ControlStatusDisplayCaptureActive);
    if (display_capture_active && !scene_graph_ready) {
        return false;
    }
    return running == (snapshot.hud_process_id != 0U);
''',
)

replace_once(
    "tests/control-status-reader-test.cpp",
    '''    publish(
        mapped.get(), chatview::ControlStatusHudVisible, 0U, 9U, {});
    if (reader.read(snapshot)) {
        return fail(L"A visible HUD without a running process was accepted");
    }

    chatview::RuntimeTelemetrySnapshot invalid = telemetry;
''',
    '''    publish(
        mapped.get(), chatview::ControlStatusHudVisible, 0U, 9U, {});
    if (reader.read(snapshot)) {
        return fail(L"A visible HUD without a running process was accepted");
    }

    publish(
        mapped.get(),
        chatview::ControlStatusDisplayCaptureActive,
        0U,
        10U,
        {});
    if (reader.read(snapshot)) {
        return fail(
            L"Active Display Capture without a stable scene graph was accepted");
    }

    chatview::RuntimeTelemetrySnapshot invalid = telemetry;
''',
)
replace_once(
    "tests/control-status-reader-test.cpp",
    '''    publish(mapped.get(), chatview::ControlStatusNone, 0U, 10U, invalid);''',
    '''    publish(mapped.get(), chatview::ControlStatusNone, 0U, 11U, invalid);''',
)

replace_once(
    "src/plugin/plugin-main.cpp",
    '''constexpr UINT kCaptureRiskPollIntervalMs = 100U;
constexpr ULONGLONG kOutputStartPendingTimeoutMs = 15000U;
''',
    '''constexpr UINT kCaptureRiskPollIntervalMs = 100U;
constexpr ULONGLONG kIdleCaptureScanIntervalMs = 500U;
constexpr ULONGLONG kOutputStartPendingTimeoutMs = 15000U;
''',
)
replace_once(
    "src/plugin/plugin-main.cpp",
    '''ULONGLONG conservative_scan_deadline = 0U;
bool published_capture_state = false;
''',
    '''ULONGLONG conservative_scan_deadline = 0U;
ULONGLONG next_idle_capture_scan_tick = 0U;
CaptureScan cached_capture_scan;
bool published_capture_state = false;
''',
)
replace_once(
    "src/plugin/plugin-main.cpp",
    '''    CaptureScan scan;
    if (scene_graph_stable) {
        scan = scan_display_capture_sources();
    }
''',
    '''    CaptureScan scan;
    if (scene_graph_stable) {
        if (output_active || now >= next_idle_capture_scan_tick) {
            cached_capture_scan = scan_display_capture_sources();
            next_idle_capture_scan_tick =
                now + kIdleCaptureScanIntervalMs;
        }
        scan = cached_capture_scan;
    } else {
        cached_capture_scan = {};
        next_idle_capture_scan_tick = 0U;
    }
''',
)
replace_once(
    "src/plugin/plugin-main.cpp",
    '''    conservative_scan_deadline = 0U;
    published_capture_state = false;
''',
    '''    conservative_scan_deadline = 0U;
    next_idle_capture_scan_tick = 0U;
    cached_capture_scan = {};
    published_capture_state = false;
''',
)

path = Path("src/plugin/plugin-main.cpp")
text = path.read_text(encoding="utf-8")
for anchor in (
    "case OBS_FRONTEND_EVENT_FINISHED_LOADING:\n",
    "case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:\n",
    "case OBS_FRONTEND_EVENT_SCENE_CHANGED:\n",
):
    if text.count(anchor) != 1:
        raise SystemExit(f"src/plugin/plugin-main.cpp: expected one event anchor, found {text.count(anchor)}")

text = text.replace(
    '''    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        initialize_scene_graph_state();
        break;
''',
    '''    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        next_idle_capture_scan_tick = 0U;
        initialize_scene_graph_state();
        break;
''',
    1,
)
text = text.replace(
    '''    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        scene_graph_stable = false;
        break;
''',
    '''    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        scene_graph_stable = false;
        cached_capture_scan = {};
        next_idle_capture_scan_tick = 0U;
        break;
''',
    1,
)
text = text.replace(
    '''    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
''',
    '''    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
        next_idle_capture_scan_tick = 0U;
        break;

    case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
''',
    1,
)
path.write_text(text, encoding="utf-8", newline="\n")
