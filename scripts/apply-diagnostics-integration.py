from pathlib import Path
import re


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path_text}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


for required in (
    "src/diagnostics/main.cpp",
    "src/diagnostics/report.cpp",
    "src/diagnostics/report.hpp",
    "tests/diagnostics-report-test.cpp",
):
    if not Path(required).is_file():
        raise SystemExit(f"missing diagnostics source: {required}")

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.3.6 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.3.7 LANGUAGES CXX)",
)

replace_once(
    "CMakeLists.txt",
    "add_executable(chat-view-self-test\n",
    """add_executable(chat-view-diagnostics WIN32
    src/diagnostics/main.cpp
    src/diagnostics/report.cpp
    src/diagnostics/report.hpp
    src/common/chat-config.hpp
    src/common/win32-handle.hpp
    src/common/window-messages.hpp
)
target_include_directories(
    chat-view-diagnostics PRIVATE "${CHATVIEW_SOURCE_DIR}")
target_link_libraries(
    chat-view-diagnostics PRIVATE chat-view-common user32 shell32 ole32)
target_compile_definitions(
    chat-view-diagnostics PRIVATE CHATVIEW_VERSION="${PROJECT_VERSION}")
chatview_enable_win32(chat-view-diagnostics)
set_target_properties(
    chat-view-diagnostics PROPERTIES OUTPUT_NAME "chat-view-diagnostics")
chatview_enable_warnings(chat-view-diagnostics)

add_executable(chat-view-self-test
""",
)

replace_once(
    "CMakeLists.txt",
    "    add_executable(chat-view-hud-health-test\n",
    """    add_executable(chat-view-diagnostics-report-test
        tests/diagnostics-report-test.cpp
        src/diagnostics/report.cpp
        src/diagnostics/report.hpp
    )
    target_include_directories(
        chat-view-diagnostics-report-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-diagnostics-report-test)
    add_test(
        NAME chat-view-diagnostics-report
        COMMAND chat-view-diagnostics-report-test
    )

    add_executable(chat-view-hud-health-test
""",
)

cmake_path = Path("CMakeLists.txt")
cmake = cmake_path.read_text(encoding="utf-8")
blocks = list(re.finditer(r"install\(TARGETS(?P<body>.*?)\n\)", cmake, re.S))
matches = [
    match
    for match in blocks
    if "chat-view-self-test" in match.group("body")
    and "chat-view-plugin-preflight" in match.group("body")
]
if len(matches) != 1:
    raise SystemExit(f"expected one support-tool install block, found {len(matches)}")
block_match = matches[0]
block = block_match.group(0)
if "chat-view-diagnostics" in block:
    raise SystemExit("diagnostics target already present in install block")
lines = block.splitlines()
index = next(
    position
    for position, line in enumerate(lines)
    if "chat-view-plugin-preflight" in line
) + 1
indent = re.match(r"\s*", lines[index - 1]).group(0)
lines.insert(index, indent + "chat-view-diagnostics")
cmake = cmake[: block_match.start()] + "\n".join(lines) + cmake[block_match.end() :]
cmake_path.write_text(cmake, encoding="utf-8", newline="\n")

replace_once(
    ".github/workflows/windows-build.yml",
    """            'dist/chat-view-self-test.exe',
            'dist/chat-view-plugin-preflight.exe'
""",
    """            'dist/chat-view-self-test.exe',
            'dist/chat-view-plugin-preflight.exe',
            'dist/chat-view-diagnostics.exe'
""",
)

replace_once(
    ".github/workflows/windows-build.yml",
    """            'dist/chat-view-self-test.exe',
            'dist/chat-view-plugin-preflight.exe',
            'dist/data/obs-plugins/chat-view-obs/locale/en-US.ini',
""",
    """            'dist/chat-view-self-test.exe',
            'dist/chat-view-plugin-preflight.exe',
            'dist/chat-view-diagnostics.exe',
            'dist/data/obs-plugins/chat-view-obs/locale/en-US.ini',
""",
)

readme_path = Path("README.md")
readme = readme_path.read_text(encoding="utf-8")
status_line = "- pinned Windows CI, native tests, installer test, and packaged artifact."
if readme.count(status_line) != 1:
    raise SystemExit("README status anchor was not unique")
readme = readme.replace(
    status_line,
    status_line
    + "\n- privacy-safe diagnostics export that omits raw chat URLs, messages, "
    + "tokens, cookies, and browser profiles.",
    1,
)
tree_anchor = "├── ensure-webview2-runtime.ps1\n├── README.md"
if readme.count(tree_anchor) != 1:
    raise SystemExit("README install-tree anchor was not unique")
readme = readme.replace(
    tree_anchor,
    "├── ensure-webview2-runtime.ps1\n├── chat-view-diagnostics.exe\n├── README.md",
    1,
)
readme_path.write_text(readme, encoding="utf-8", newline="\n")
