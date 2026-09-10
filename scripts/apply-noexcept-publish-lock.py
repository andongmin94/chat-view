from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected exactly one anchor in {path}, found {count}: {old[:120]!r}"
        )
    write(path, content.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    content = read(path)
    count = content.count(old)
    if count != expected:
        raise RuntimeError(
            f"Expected {expected} anchors in {path}, found {count}: {old!r}"
        )
    write(path, content.replace(old, new))


def replace_region(path: str, start: str, end: str, replacement: str) -> None:
    content = read(path)
    start_index = content.find(start)
    if start_index < 0:
        raise RuntimeError(f"Start marker not found in {path}: {start!r}")
    end_index = content.find(end, start_index)
    if end_index < 0:
        raise RuntimeError(f"End marker not found in {path}: {end!r}")
    if content.find(start, start_index + len(start)) >= 0:
        raise RuntimeError(f"Start marker is not unique in {path}: {start!r}")
    write(path, content[:start_index] + replacement + content[end_index:])


srw_lock = r'''// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <Windows.h>

namespace chatview {

class SharedSrwLockGuard final {
public:
    explicit SharedSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockShared(lock_);
    }

    ~SharedSrwLockGuard()
    {
        ReleaseSRWLockShared(lock_);
    }

    SharedSrwLockGuard(const SharedSrwLockGuard &) = delete;
    SharedSrwLockGuard &operator=(const SharedSrwLockGuard &) = delete;

private:
    SRWLOCK *lock_ = nullptr;
};

class ExclusiveSrwLockGuard final {
public:
    explicit ExclusiveSrwLockGuard(SRWLOCK &lock) noexcept : lock_(&lock)
    {
        AcquireSRWLockExclusive(lock_);
    }

    ~ExclusiveSrwLockGuard()
    {
        ReleaseSRWLockExclusive(lock_);
    }

    ExclusiveSrwLockGuard(const ExclusiveSrwLockGuard &) = delete;
    ExclusiveSrwLockGuard &operator=(const ExclusiveSrwLockGuard &) = delete;

private:
    SRWLOCK *lock_ = nullptr;
};

} // namespace chatview
'''
write("src/common/srw-lock.hpp", srw_lock)

replace_once(
    "src/plugin/runtime-controller.hpp",
    '#include "common/shared-state.hpp"\n#include "common/win32-handle.hpp"\n',
    '#include "common/shared-state.hpp"\n#include "common/srw-lock.hpp"\n#include "common/win32-handle.hpp"\n',
)
replace_once(
    "src/plugin/runtime-controller.hpp",
    "#include <mutex>\n#include <shared_mutex>\n#include <string>\n",
    "#include <mutex>\n#include <string>\n",
)
replace_once(
    "src/plugin/runtime-controller.hpp",
    "    std::shared_mutex state_publish_mutex_;\n",
    "    SRWLOCK state_publish_lock_ = SRWLOCK_INIT;\n",
)

replace_once(
    "src/plugin/runtime-controller.cpp",
    "#include <filesystem>\n#include <shared_mutex>\n#include <string>\n",
    "#include <filesystem>\n#include <string>\n",
)
replace_count(
    "src/plugin/runtime-controller.cpp",
    "std::unique_lock publish_lock(state_publish_mutex_);",
    "ExclusiveSrwLockGuard publish_lock(state_publish_lock_);",
    3,
)
replace_count(
    "src/plugin/runtime-controller.cpp",
    "std::shared_lock publish_lock(state_publish_mutex_);",
    "SharedSrwLockGuard publish_lock(state_publish_lock_);",
    2,
)

replace_once(
    "src/hud/webview-host.cpp",
    '#include "common/chat-config.hpp"\n',
    '#include "common/chat-config.hpp"\n#include "hud/host-state-message.hpp"\n',
)

bootstrap = r'''constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(
(() => {
  if (window.top !== window || !window.chrome || !window.chrome.webview) return;

  const defaultTone = '#aeb0b2';
  const host = document.createElement('chatview-private-hud-root');
  const root = host.attachShadow({ mode: 'closed' });
  const shadowStyle = document.createElement('style');
  shadowStyle.textContent = `
    :host { all: initial; }
    .frame {
      position: fixed;
      inset: 0;
      box-sizing: border-box;
      border: 3px solid #5ac8fa;
      opacity: 0;
      transition: opacity .12s ease;
      pointer-events: none;
    }
    .edit {
      position: absolute;
      left: 12px;
      top: 10px;
      padding: 8px 11px;
      border-radius: 10px;
      background: rgba(20,20,24,.92);
      color: #fff;
      font: 600 13px/1.2 "Segoe UI",sans-serif;
      box-shadow: 0 6px 24px rgba(0,0,0,.35);
    }
    .status {
      position: absolute;
      right: 12px;
      top: 10px;
      display: none;
      align-items: center;
      gap: 7px;
      padding: 7px 10px;
      border-radius: 999px;
      background: rgba(20,20,24,.82);
      color: #fff;
      font: 700 12px/1 "Segoe UI",sans-serif;
      box-shadow: 0 5px 20px rgba(0,0,0,.30);
    }
    .dot {
      width: 8px;
      height: 8px;
      border-radius: 999px;
      background: ${defaultTone};
      box-shadow: 0 0 10px ${defaultTone};
    }
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
  const statusText = document.createElement('span');
  status.append(dot, statusText);
  root.append(shadowStyle, frame, status);

  const transparencyCss = `
    html, body, body > #root, body > #__next {
      background: transparent !important;
      background-color: transparent !important;
    }
    html {
      --yt-live-chat-background-color: transparent !important;
      --yt-live-chat-secondary-background-color: rgba(18, 18, 22, .70) !important;
      --yt-live-chat-tertiary-background-color: rgba(18, 18, 22, .82) !important;
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
  const pageStyle = document.createElement('style');
  pageStyle.textContent = transparencyCss;

  const restoreHostShell = () => {
    host.removeAttribute('hidden');
    host.style.setProperty('all', 'initial', 'important');
    host.style.setProperty('position', 'fixed', 'important');
    host.style.setProperty('inset', '0', 'important');
    host.style.setProperty('display', 'block', 'important');
    host.style.setProperty('visibility', 'visible', 'important');
    host.style.setProperty('opacity', '1', 'important');
    host.style.setProperty('z-index', '2147483647', 'important');
    host.style.setProperty('pointer-events', 'none', 'important');
  };

  let observer = null;
  const ensureInstalled = () => {
    try {
      const documentRoot = document.documentElement;
      if (!documentRoot) {
        setTimeout(ensureInstalled, 0);
        return;
      }

      restoreHostShell();
      if (host.parentNode !== documentRoot ||
          documentRoot.lastElementChild !== host) {
        documentRoot.appendChild(host);
      }

      if (pageStyle.textContent !== transparencyCss) {
        pageStyle.textContent = transparencyCss;
      }
      const styleParent = document.head || documentRoot;
      if (pageStyle.parentNode !== styleParent) {
        styleParent.appendChild(pageStyle);
      }

      if (observer) {
        observer.observe(styleParent, { childList: true });
      }
    } catch (_) {
      setTimeout(ensureInstalled, 50);
    }
  };

  const applyState = (candidate) => {
    if (!candidate || candidate.type !== 'host-state' ||
        typeof candidate.editing !== 'boolean') {
      return;
    }

    const statusValue = typeof candidate.status === 'string'
      ? candidate.status.slice(0, 96)
      : '';
    const toneValue = typeof candidate.tone === 'string' &&
      /^#[0-9a-fA-F]{6}$/.test(candidate.tone)
        ? candidate.tone
        : defaultTone;

    ensureInstalled();
    frame.style.opacity = candidate.editing ? '1' : '0';
    status.style.display = statusValue ? 'flex' : 'none';
    statusText.textContent = statusValue;
    dot.style.background = toneValue;
    dot.style.boxShadow = `0 0 10px ${toneValue}`;
  };

  observer = new MutationObserver(ensureInstalled);
  ensureInstalled();
  if (document.documentElement) {
    observer.observe(document.documentElement, { childList: true });
  }
  observer.observe(host, {
    attributes: true,
    attributeFilter: ['style', 'hidden']
  });

  window.chrome.webview.addEventListener('message', (event) => {
    applyState(event.data);
  });
})();
)JS";

'''
replace_region(
    "src/hud/webview-host.cpp",
    'constexpr wchar_t kOverlayBootstrapScript[] = LR"JS(\n',
    "std::wstring javascript_string",
    bootstrap,
)
replace_region(
    "src/hud/webview-host.cpp",
    "std::wstring javascript_string",
    "HRESULT create_d3d_device",
    "",
)
replace_once(
    "src/hud/webview-host.cpp",
    "FAILED(settings->put_IsWebMessageEnabled(FALSE))",
    "FAILED(settings->put_IsWebMessageEnabled(TRUE))",
)
old_apply = r'''void WebViewHost::apply_host_state() noexcept
{
    if (!ready_ || !webview_) {
        return;
    }

    std::wstring script =
        L"window.__chatviewEnsureHost && window.__chatviewEnsureHost();"
        L"window.__chatviewApplyHostState && "
        L"window.__chatviewApplyHostState({editing:";
    script.append(editing_ ? L"true" : L"false");
    script.append(L",status:");
    script.append(javascript_string(status_text_));
    script.append(L",tone:");
    script.append(javascript_string(status_tone_));
    script.append(L"});");
    webview_->ExecuteScript(script.c_str(), nullptr);
}
'''
new_apply = r'''void WebViewHost::apply_host_state() noexcept
{
    if (!ready_ || !webview_) {
        return;
    }

    const std::wstring message = serialize_host_state_message(
        editing_, status_text_, status_tone_);
    if (message.empty()) {
        post_failure(E_OUTOFMEMORY);
        return;
    }

    const HRESULT result = webview_->PostWebMessageAsJson(message.c_str());
    if (FAILED(result)) {
        post_failure(result);
    }
}
'''
replace_once("src/hud/webview-host.cpp", old_apply, new_apply)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.7 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.8 LANGUAGES CXX)",
)
replace_once(
    "CMakeLists.txt",
    "    src/plugin/transport-token.hpp\n"
    "    src/common/shared-state.hpp\n"
    "    src/common/win32-handle.hpp\n",
    "    src/plugin/transport-token.hpp\n"
    "    src/common/shared-state.hpp\n"
    "    src/common/srw-lock.hpp\n"
    "    src/common/win32-handle.hpp\n",
)
replace_once(
    "CMakeLists.txt",
    "add_executable(chat-view-hud WIN32\n"
    "    src/hud/main.cpp\n"
    "    src/hud/hud-placement.cpp\n",
    "add_executable(chat-view-hud WIN32\n"
    "    src/hud/main.cpp\n"
    "    src/hud/host-state-message.cpp\n"
    "    src/hud/host-state-message.hpp\n"
    "    src/hud/hud-placement.cpp\n",
)
replace_once(
    "CMakeLists.txt",
    "    add_test(\n"
    "        NAME chat-view-restart-policy\n"
    "        COMMAND chat-view-restart-policy-test\n"
    "    )\n\n"
    "    add_test(\n"
    "        NAME chat-view-hud-smoke\n",
    "    add_test(\n"
    "        NAME chat-view-restart-policy\n"
    "        COMMAND chat-view-restart-policy-test\n"
    "    )\n\n"
    "    add_executable(chat-view-host-state-message-test\n"
    "        tests/host-state-message-test.cpp\n"
    "        src/hud/host-state-message.cpp\n"
    "        src/hud/host-state-message.hpp\n"
    "    )\n"
    "    target_include_directories(\n"
    "        chat-view-host-state-message-test PRIVATE \"${CHATVIEW_SOURCE_DIR}\")\n"
    "    chatview_enable_warnings(chat-view-host-state-message-test)\n"
    "    add_test(\n"
    "        NAME chat-view-host-state-message\n"
    "        COMMAND chat-view-host-state-message-test\n"
    "    )\n\n"
    "    add_test(\n"
    "        NAME chat-view-hud-smoke\n",
)

architecture_path = "docs/architecture.md"
architecture = read(architecture_path)
architecture = architecture.replace(
    "- injects a small isolated Shadow DOM control layer for edit bounds and OBS status;",
    "- injects a closed Shadow DOM control layer for edit bounds and OBS status;",
    1,
)
architecture_anchor = (
    "- forces document and body backgrounds transparent without rewriting the provider UI."
)
if architecture_anchor not in architecture:
    raise RuntimeError("Could not locate the WebView architecture anchor")
architecture = architecture.replace(
    architecture_anchor,
    architecture_anchor
    + "\n\nOBS state reaches that closed control layer only through native-to-page JSON messages. The provider page receives no callable ChatView control function, and the native host registers no page-to-native message handler.",
    1,
)
fault_anchor = (
    "- never perform browser, network, or rendering work on an OBS callback thread."
)
if fault_anchor not in architecture:
    raise RuntimeError("Could not locate the callback-thread architecture anchor")
architecture = architecture.replace(
    fault_anchor,
    fault_anchor
    + "\n\nThe callback wake handle is protected with non-throwing Win32 SRW locks. No C++ lock construction occurs in the `noexcept` frontend callback path.",
    1,
)
write(architecture_path, architecture)

readme_path = "README.md"
readme = read(readme_path)
runtime_anchor = (
    "The renderer remains out of process deliberately. A browser or desktop-rendering failure must not take down OBS Studio."
)
if runtime_anchor not in readme:
    raise RuntimeError("Could not locate README renderer boundary anchor")
readme = readme.replace(
    runtime_anchor,
    runtime_anchor
    + " The provider page cannot call ChatView control functions: native status is delivered one way into a closed Shadow DOM layer.",
    1,
)
write(readme_path, readme)

webview = read("src/hud/webview-host.cpp")
if "window.__chatview" in webview or "ExecuteScript" in webview:
    raise RuntimeError("Legacy page-global ChatView control surface remains")
for required in (
    "attachShadow({ mode: 'closed' })",
    "PostWebMessageAsJson",
    "put_IsWebMessageEnabled(TRUE)",
    '#include "hud/host-state-message.hpp"',
):
    if required not in webview:
        raise RuntimeError(f"Missing WebView hardening marker: {required}")

cmake = read("CMakeLists.txt")
for required in (
    "src/common/srw-lock.hpp",
    "src/hud/host-state-message.cpp",
    "chat-view-host-state-message-test",
):
    if required not in cmake:
        raise RuntimeError(f"Missing build integration: {required}")

for obsolete in (
    ".github/workflows/apply-noexcept-publish-lock.yml",
    "scripts/apply-noexcept-publish-lock.py",
):
    target = ROOT / obsolete
    if target.exists():
        target.unlink()
