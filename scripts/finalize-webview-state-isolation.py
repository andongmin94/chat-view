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


HOST_STATE_HEADER = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace chatview {

[[nodiscard]] std::wstring make_host_state_message(
    bool editing,
    const std::wstring &status_text,
    const std::wstring &status_tone);

} // namespace chatview
'''

HOST_STATE_SOURCE = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/host-state-message.hpp"

#include <string>

namespace chatview {
namespace {

void append_json_string(std::wstring &output, const std::wstring &value)
{
    constexpr wchar_t hex[] = L"0123456789abcdef";

    output.push_back(L'"');
    for (const wchar_t character : value) {
        switch (character) {
        case L'"':
            output.append(L"\\\"");
            break;
        case L'\\':
            output.append(L"\\\\");
            break;
        case L'\b':
            output.append(L"\\b");
            break;
        case L'\f':
            output.append(L"\\f");
            break;
        case L'\n':
            output.append(L"\\n");
            break;
        case L'\r':
            output.append(L"\\r");
            break;
        case L'\t':
            output.append(L"\\t");
            break;
        default:
            if (static_cast<unsigned int>(character) < 0x20U) {
                const unsigned int value =
                    static_cast<unsigned int>(character);
                output.append(L"\\u00");
                output.push_back(hex[(value >> 4U) & 0xFU]);
                output.push_back(hex[value & 0xFU]);
            } else {
                output.push_back(character);
            }
            break;
        }
    }
    output.push_back(L'"');
}

} // namespace

std::wstring make_host_state_message(
    bool editing,
    const std::wstring &status_text,
    const std::wstring &status_tone)
{
    std::wstring output;
    output.reserve(96U + status_text.size() + status_tone.size());
    output.append(
        L"{\"type\":\"chatview-host-state\",\"editing\":");
    output.append(editing ? L"true" : L"false");
    output.append(L",\"status\":");
    append_json_string(output, status_text);
    output.append(L",\"tone\":");
    append_json_string(output, status_tone);
    output.push_back(L'}');
    return output;
}

} // namespace chatview
'''

HOST_STATE_TEST = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/host-state-message.hpp"

#include <iostream>
#include <string>

namespace {

bool expect_equal(
    const std::wstring &actual,
    const std::wstring &expected,
    const wchar_t *message)
{
    if (actual == expected) {
        return true;
    }
    std::wcerr << message << L"\nactual:   " << actual
               << L"\nexpected: " << expected << L'\n';
    return false;
}

} // namespace

int main()
{
    if (!expect_equal(
            chatview::make_host_state_message(
                true, L"LIVE • REC", L"#ff453a"),
            L"{\"type\":\"chatview-host-state\",\"editing\":true,"
            L"\"status\":\"LIVE • REC\",\"tone\":\"#ff453a\"}",
            L"Normal host state was not serialized correctly") ||
        !expect_equal(
            chatview::make_host_state_message(
                false,
                L"quote=\" slash=\\ line\n tab\t back\b form\f return\r",
                L""),
            L"{\"type\":\"chatview-host-state\",\"editing\":false,"
            L"\"status\":\"quote=\\\" slash=\\\\ line\\n tab\\t "
            L"back\\b form\\f return\\r\",\"tone\":\"\"}",
            L"JSON escapes were not serialized correctly") ||
        !expect_equal(
            chatview::make_host_state_message(
                false,
                std::wstring(L"control=") + static_cast<wchar_t>(1),
                L"#aeb0b2"),
            L"{\"type\":\"chatview-host-state\",\"editing\":false,"
            L"\"status\":\"control=\\u0001\","
            L"\"tone\":\"#aeb0b2\"}",
            L"Control characters were not escaped") ||
        !expect_equal(
            chatview::make_host_state_message(
                false, L"방송 준비", L"#34c759"),
            L"{\"type\":\"chatview-host-state\",\"editing\":false,"
            L"\"status\":\"방송 준비\",\"tone\":\"#34c759\"}",
            L"Unicode host state was not preserved")) {
        return 1;
    }
    return 0;
}
'''

SECURE_BOOTSTRAP = r'''constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(
(() => {
  if (window.top !== window || !window.chrome || !window.chrome.webview) return;

  const state = { editing: false, status: '', tone: '#aeb0b2' };
  let pageStyle = null;
  let host = null;
  let statusText = null;
  let observer = null;

  const setImportant = (element, name, value) => {
    element.style.setProperty(name, value, 'important');
  };

  const apply = () => {
    if (!host || !statusText) return;
    host.setAttribute('data-chatview-editing', state.editing ? '1' : '0');
    host.setAttribute('data-chatview-has-status', state.status ? '1' : '0');
    host.style.setProperty('--chatview-tone', state.tone, 'important');
    statusText.textContent = state.status;
  };

  const mount = () => {
    const documentElement = document.documentElement;
    if (!documentElement) {
      setTimeout(mount, 0);
      return;
    }

    if (!pageStyle) {
      pageStyle = document.createElement('style');
      pageStyle.textContent = `
        html, body, body > #root, body > #__next {
          background: transparent !important;
          background-color: transparent !important;
        }
        html {
          --yt-live-chat-background-color: transparent !important;
          --yt-live-chat-secondary-background-color: rgba(18,18,22,.70) !important;
          --yt-live-chat-tertiary-background-color: rgba(18,18,22,.82) !important;
        }
        yt-live-chat-app,
        yt-live-chat-renderer,
        yt-live-chat-renderer #contents,
        yt-live-chat-renderer #item-list,
        yt-live-chat-renderer #chat {
          background: transparent !important;
          background-color: transparent !important;
        }
        ::-webkit-scrollbar { display: none !important; }
      `;
    }
    if (!pageStyle.isConnected) {
      documentElement.appendChild(pageStyle);
    }

    if (!host) {
      host = document.createElement('div');
      host.setAttribute('aria-hidden', 'true');
      setImportant(host, 'all', 'initial');
      setImportant(host, 'position', 'fixed');
      setImportant(host, 'inset', '0');
      setImportant(host, 'display', 'block');
      setImportant(host, 'z-index', '2147483647');
      setImportant(host, 'pointer-events', 'none');

      const root = host.attachShadow({ mode: 'closed' });
      const shadowStyle = document.createElement('style');
      shadowStyle.textContent = `
        :host { all: initial; }
        .frame {
          position: fixed; inset: 0; box-sizing: border-box;
          border: 3px solid #5ac8fa; opacity: 0;
          transition: opacity .12s ease; pointer-events: none;
        }
        .edit {
          position: absolute; left: 12px; top: 10px; padding: 8px 11px;
          border-radius: 10px; background: rgba(20,20,24,.92);
          color: #fff; font: 600 13px/1.2 "Segoe UI",sans-serif;
          box-shadow: 0 6px 24px rgba(0,0,0,.35);
        }
        .status {
          position: absolute; right: 12px; top: 10px; display: none;
          align-items: center; gap: 7px; padding: 7px 10px;
          border-radius: 999px; background: rgba(20,20,24,.82);
          color: #fff; font: 700 12px/1 "Segoe UI",sans-serif;
          box-shadow: 0 5px 20px rgba(0,0,0,.30);
        }
        .dot {
          width: 8px; height: 8px; border-radius: 999px;
          background: var(--chatview-tone,#aeb0b2);
          box-shadow: 0 0 10px var(--chatview-tone,#aeb0b2);
        }
        :host([data-chatview-editing="1"]) .frame { opacity: 1; }
        :host([data-chatview-has-status="1"]) .status { display: flex; }
      `;

      const frame = document.createElement('div');
      frame.className = 'frame';
      const edit = document.createElement('div');
      edit.className = 'edit';
      edit.textContent = 'DRAG HEADER · RESIZE EDGES · CTRL+ALT+SHIFT+H TO LOCK';
      frame.appendChild(edit);

      const status = document.createElement('div');
      status.className = 'status';
      const dot = document.createElement('span');
      dot.className = 'dot';
      statusText = document.createElement('span');
      status.append(dot, statusText);
      root.append(shadowStyle, frame, status);
    }

    setImportant(host, 'display', 'block');
    setImportant(host, 'position', 'fixed');
    setImportant(host, 'inset', '0');
    setImportant(host, 'z-index', '2147483647');
    setImportant(host, 'pointer-events', 'none');
    if (!host.isConnected) {
      documentElement.appendChild(host);
    }

    if (!observer) {
      observer = new MutationObserver(() => {
        if (!pageStyle.isConnected || !host.isConnected) mount();
      });
      observer.observe(documentElement, { childList: true });
    }
    apply();
  };

  window.chrome.webview.addEventListener('message', (event) => {
    const payload = event.data;
    if (!payload || payload.type !== 'chatview-host-state') return;
    state.editing = payload.editing === true;
    state.status = typeof payload.status === 'string'
      ? payload.status.slice(0, 128)
      : '';
    state.tone = typeof payload.tone === 'string' &&
      /^#[0-9a-fA-F]{6}$/.test(payload.tone)
      ? payload.tone
      : '#aeb0b2';
    mount();
  });

  mount();
})();
)JS";'''

write("src/hud/host-state-message.hpp", HOST_STATE_HEADER)
write("src/hud/host-state-message.cpp", HOST_STATE_SOURCE)
write("tests/host-state-message-test.cpp", HOST_STATE_TEST)

source_path = "src/hud/webview-host.cpp"
source = read(source_path)
include_anchor = '#include "common/chat-config.hpp"\n'
include_line = '#include "hud/host-state-message.hpp"\n'
if include_line not in source:
    if include_anchor not in source:
        raise RuntimeError("Could not locate include anchor")
    source = source.replace(include_anchor, include_anchor + include_line, 1)

if "window.__chatview" in source:
    start = source.find('constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(')
    if start < 0:
        raise RuntimeError("Could not find legacy bootstrap start")
    end = source.find('\n)JS";', start)
    if end < 0:
        raise RuntimeError("Could not find legacy bootstrap end")
    source = source[:start] + SECURE_BOOTSTRAP + source[end + len('\n)JS";'):]
elif "chatview-host-state" not in source or "attachShadow({ mode: 'closed' })" not in source:
    raise RuntimeError("WebView bootstrap is neither legacy nor isolated")

helper_start = source.find("std::wstring javascript_string(")
if helper_start >= 0:
    helper_end = source.find("HRESULT create_d3d_device", helper_start)
    if helper_end < 0:
        raise RuntimeError("Could not find JavaScript helper end")
    source = source[:helper_start] + source[helper_end:]

source = source.replace(
    "settings->put_IsWebMessageEnabled(FALSE)",
    "settings->put_IsWebMessageEnabled(TRUE)",
)
if "settings->put_IsWebMessageEnabled(TRUE)" not in source:
    raise RuntimeError("Web messaging is not enabled")

if "ExecuteScript(" in source:
    apply_start = source.find("void WebViewHost::apply_host_state() noexcept")
    namespace_end = source.rfind("\n} // namespace chatview")
    if apply_start < 0 or namespace_end < apply_start:
        raise RuntimeError("Could not isolate apply_host_state implementation")
    replacement = r'''void WebViewHost::apply_host_state() noexcept
{
    if (!ready_ || !webview_) {
        return;
    }

    const std::wstring message = make_host_state_message(
        editing_, status_text_, status_tone_);
    const HRESULT result = webview_->PostWebMessageAsJson(message.c_str());
    if (FAILED(result)) {
        post_failure(result);
    }
}
'''
    source = source[:apply_start] + replacement + source[namespace_end:]

for forbidden in (
    "window.__chatview",
    "javascript_string(",
    "ExecuteScript(",
    "attachShadow({ mode: 'open' })",
):
    if forbidden in source:
        raise RuntimeError(f"Legacy page-control surface remains: {forbidden}")
if source.count("PostWebMessageAsJson") != 1:
    raise RuntimeError("Expected exactly one native host-state message send")
write(source_path, source)

cmake_path = "CMakeLists.txt"
cmake = read(cmake_path)
project_match = re.search(
    r"project\(chat-view-obs VERSION ([0-9]+)\.([0-9]+)\.([0-9]+) LANGUAGES CXX\)",
    cmake,
)
if project_match is None:
    raise RuntimeError("Could not locate project version")
version = tuple(int(part) for part in project_match.groups())
if version < (0, 2, 9):
    cmake = cmake[:project_match.start()] + (
        "project(chat-view-obs VERSION 0.2.9 LANGUAGES CXX)"
    ) + cmake[project_match.end():]

if "src/hud/host-state-message.cpp" not in cmake:
    anchor = "add_executable(chat-view-hud WIN32\n    src/hud/main.cpp\n"
    if anchor not in cmake:
        raise RuntimeError("Could not locate HUD source list")
    cmake = cmake.replace(
        anchor,
        anchor
        + "    src/hud/host-state-message.cpp\n"
          "    src/hud/host-state-message.hpp\n",
        1,
    )

if "chat-view-host-state-message-test" not in cmake:
    anchor = "if(BUILD_TESTING)\n"
    if anchor not in cmake:
        raise RuntimeError("Could not locate test block")
    block = r'''if(BUILD_TESTING)
    add_executable(chat-view-host-state-message-test
        tests/host-state-message-test.cpp
        src/hud/host-state-message.cpp
        src/hud/host-state-message.hpp
    )
    target_include_directories(
        chat-view-host-state-message-test PRIVATE "${CHATVIEW_SOURCE_DIR}")
    chatview_enable_warnings(chat-view-host-state-message-test)
    add_test(
        NAME chat-view-host-state-message
        COMMAND chat-view-host-state-message-test
    )

'''
    cmake = cmake.replace(anchor, block, 1)
write(cmake_path, cmake)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
old = "- injects a small isolated Shadow DOM control layer for edit bounds and OBS status;"
new = "- sends bounded host state through a one-way JSON message into a closed Shadow DOM control layer;"
architecture = architecture.replace(old, new)
if new not in architecture:
    raise RuntimeError("Architecture did not converge on isolated host state")
write(architecture_path, architecture)

for temporary in (
    ".github/workflows/apply-webview-state-isolation.yml",
    "scripts/apply-webview-state-isolation.py",
    ".github/workflows/finalize-webview-state-isolation.yml",
    "scripts/finalize-webview-state-isolation.py",
):
    (ROOT / temporary).unlink(missing_ok=True)
