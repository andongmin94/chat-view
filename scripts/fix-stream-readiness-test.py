from pathlib import Path

path = Path("tests/config-single-instance-test.cpp")
text = path.read_text(encoding="utf-8")

replacements = [
    (
        '''                       child_text_contains(
                           control_center, L"YouTube chat ready");
                       child_text_contains(
                           control_center,
                           L"BLOCKED — Save a supported chat URL");''',
        '''                       child_text_contains(
                           control_center, L"YouTube chat ready") &&
                       child_text_contains(
                           control_center,
                           L"BLOCKED — Save a supported chat URL");''',
    ),
    (
        '''                       saved_url_matches(
                           config_file,
                           L"https://www.youtube.com/live_chat?is_popout=1&v=dQw4w9WgXcQ");
                       child_text_contains(
                           control_center, L"READY TO STREAM");''',
        '''                       saved_url_matches(
                           config_file,
                           L"https://www.youtube.com/live_chat?is_popout=1&v=dQw4w9WgXcQ") &&
                       child_text_contains(
                           control_center, L"READY TO STREAM");''',
    ),
    (
        '''    if (!post_command(control_center, kEditButtonId, edit_button) ||
            first.process.get());
    }

    publish(''',
        '''    publish(''',
    ),
]

for old, new in replacements:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Expected one repair anchor, found {count}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8", newline="\n")
