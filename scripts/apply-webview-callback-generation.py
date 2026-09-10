from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


source_path = "src/hud/webview-host.cpp"
source = read(source_path)

if "#include <utility>\n" not in source:
    anchor = "#include <string>\n"
    if anchor not in source:
        raise RuntimeError("Could not locate standard include anchor")
    source = source.replace(anchor, anchor + "#include <utility>\n", 1)

legacy_constructor = '''WebViewHost::WebViewHost()
    : callback_state_(std::make_shared<CallbackState>())
{
    callback_state_->owner = this;
}
'''
replacement_constructor = '''WebViewHost::WebViewHost() = default;
'''
if legacy_constructor in source:
    source = source.replace(
        legacy_constructor,
        replacement_constructor,
        1,
    )
elif replacement_constructor not in source:
    raise RuntimeError("WebViewHost constructor shape was not recognized")

initialize_anchor = '''bool WebViewHost::initialize(HWND window) noexcept
{
    if (window == nullptr || window_ != nullptr) {
        return false;
    }

    window_ = window;
'''
initialize_replacement = '''bool WebViewHost::initialize(HWND window) noexcept
{
    if (window == nullptr || window_ != nullptr) {
        return false;
    }

    std::shared_ptr<CallbackState> callback_state;
    try {
        callback_state = std::make_shared<CallbackState>();
    } catch (...) {
        return false;
    }
    callback_state->owner = this;
    if (callback_state_) {
        callback_state_->owner = nullptr;
    }
    callback_state_ = std::move(callback_state);

    window_ = window;
'''
if initialize_replacement not in source:
    if initialize_anchor not in source:
        raise RuntimeError("Could not locate WebViewHost initialize anchor")
    source = source.replace(
        initialize_anchor,
        initialize_replacement,
        1,
    )

if source.count("callback_state_ = std::move(callback_state);") != 1:
    raise RuntimeError("Callback generation replacement did not converge")
if "callback_state_(std::make_shared" in source:
    raise RuntimeError("Constructor still shares callback state across generations")
write(source_path, source)

cmake_path = "CMakeLists.txt"
cmake = read(cmake_path)
match = re.search(
    r"project\(chat-view-obs VERSION ([0-9]+)\.([0-9]+)\.([0-9]+) LANGUAGES CXX\)",
    cmake,
)
if match is None:
    raise RuntimeError("Could not locate project version")
version = tuple(int(part) for part in match.groups())
if version < (0, 2, 10):
    cmake = cmake[:match.start()] + (
        "project(chat-view-obs VERSION 0.2.10 LANGUAGES CXX)"
    ) + cmake[match.end():]
write(cmake_path, cmake)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
anchor = (
    "- WebView2 asynchronous callbacks are serviced by an alertable, "
    "input-available Win32 message loop."
)
addition = (
    anchor
    + " Each controller initialization receives a fresh callback-state generation, "
      "so a late callback from a closed controller cannot attach to a replacement controller."
)
if addition not in architecture:
    if anchor not in architecture:
        raise RuntimeError("Could not locate WebView callback lifecycle statement")
    architecture = architecture.replace(anchor, addition, 1)
    write(architecture_path, architecture)

for temporary in (
    ".github/workflows/apply-webview-callback-generation.yml",
    "scripts/apply-webview-callback-generation.py",
):
    (ROOT / temporary).unlink(missing_ok=True)
