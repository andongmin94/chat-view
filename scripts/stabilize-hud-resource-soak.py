from pathlib import Path

path = Path("tests/hud-resource-soak-test.cpp")
text = path.read_text(encoding="utf-8")

replacements = [
    (
        '''constexpr unsigned int kExerciseCycles = 24U;
constexpr std::uint64_t kMaximumPrivateGrowth = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumHandleGrowth = 128ULL;
''',
        '''constexpr unsigned int kWarmupCycles = 8U;
constexpr unsigned int kExerciseCycles = 24U;
constexpr unsigned int kVerificationCycles = 12U;
constexpr std::uint64_t kMaximumPrivateGrowth = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumInitialHandleGrowth = 256ULL;
constexpr std::uint64_t kMaximumLateHandleGrowth = 64ULL;
''',
    ),
    (
        '''    for (unsigned int warmup = 0U; warmup < 3U; ++warmup) {
        if (!exercise_capture_cycle(
                window, mapped_state.get(), state_event.get()) ||
            !exercise_edit_cycle(window, toggle_message)) {
            return fail(
                L"HUD warm-up failed before resource sampling",
                child_process.get());
        }
    }
    Sleep(1500U);
''',
        '''    for (unsigned int warmup = 0U; warmup < kWarmupCycles; ++warmup) {
        if (!exercise_capture_cycle(
                window, mapped_state.get(), state_event.get()) ||
            !exercise_edit_cycle(window, toggle_message)) {
            return fail(
                L"HUD warm-up failed before resource sampling",
                child_process.get());
        }
        if ((warmup % 4U) == 0U) {
            PostMessageW(window, config_message, 0U, 0L);
        }
    }
    if (!exercise_lifecycle_cycle(window)) {
        return fail(
            L"HUD lifecycle warm-up failed before resource sampling",
            child_process.get());
    }
    Sleep(2500U);
''',
    ),
    (
        '''    Sleep(2500U);
    ResourceSample final_sample;
    if (!sample_job(child_job.get(), final_sample)) {
        return fail(
            L"Failed to sample the final HUD process tree",
            child_process.get());
    }
    update_maximum(maximum, final_sample);

    std::wcout
''',
        '''    Sleep(2500U);
    ResourceSample first_window_sample;
    if (!sample_job(child_job.get(), first_window_sample)) {
        return fail(
            L"Failed to sample the first settled HUD exercise window",
            child_process.get());
    }
    update_maximum(maximum, first_window_sample);

    for (unsigned int cycle = 0U;
         cycle < kVerificationCycles;
         ++cycle) {
        if (!exercise_capture_cycle(
                window, mapped_state.get(), state_event.get()) ||
            !exercise_edit_cycle(window, toggle_message)) {
            return fail(
                L"The HUD failed during the verification resource window",
                child_process.get());
        }
        if ((cycle % 4U) == 0U) {
            PostMessageW(window, config_message, 0U, 0L);
        }
        if (cycle == kVerificationCycles / 2U &&
            !exercise_lifecycle_cycle(window)) {
            return fail(
                L"The HUD failed during the verification lifecycle transition",
                child_process.get());
        }
    }

    Sleep(2500U);
    ResourceSample final_sample;
    if (!sample_job(child_job.get(), final_sample)) {
        return fail(
            L"Failed to sample the final HUD process tree",
            child_process.get());
    }
    update_maximum(maximum, final_sample);

    std::wcout
''',
    ),
    (
        '''        << L" user=" << baseline.user_objects << L'\\n'
        << L"HUD resource soak final: processes=" << final_sample.process_count
''',
        '''        << L" user=" << baseline.user_objects << L'\\n'
        << L"HUD resource soak first window: processes="
        << first_window_sample.process_count
        << L" handles=" << first_window_sample.handle_count
        << L" private_bytes=" << first_window_sample.private_bytes
        << L" gdi=" << first_window_sample.gdi_objects
        << L" user=" << first_window_sample.user_objects << L'\\n'
        << L"HUD resource soak final: processes=" << final_sample.process_count
''',
    ),
    (
        '''    if (final_sample.process_count >
            baseline.process_count + kMaximumProcessGrowth ||
        final_sample.handle_count >
            baseline.handle_count + kMaximumHandleGrowth ||
        final_sample.private_bytes >
            baseline.private_bytes + kMaximumPrivateGrowth ||
''',
        '''    const bool handles_kept_growing =
        first_window_sample.handle_count >
            baseline.handle_count + kMaximumInitialHandleGrowth ||
        final_sample.handle_count >
            first_window_sample.handle_count + kMaximumLateHandleGrowth;

    if (final_sample.process_count >
            baseline.process_count + kMaximumProcessGrowth ||
        handles_kept_growing ||
        final_sample.private_bytes >
            baseline.private_bytes + kMaximumPrivateGrowth ||
''',
    ),
]

for old, new in replacements:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Expected one soak-test anchor, found {count}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8", newline="\n")
