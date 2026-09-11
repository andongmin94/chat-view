// SPDX-License-Identifier: GPL-2.0-or-later

#include "hud/page-health-message.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace chatview {
namespace {

constexpr std::wstring_view kMessagePrefix = L"CVH2";
constexpr std::size_t kMaximumMessageLength = 64U;

constexpr wchar_t kPageHealthBootstrapScript[] = LR"JS(
(() => {
  if (window.top !== window || !window.chrome || !window.chrome.webview) return;

  const hostname = location.hostname.toLowerCase();
  const provider = hostname === 'weflab.com'
    ? 1
    : (hostname === 'chzzk.naver.com' || hostname === 'm.chzzk.naver.com')
      ? 2
      : hostname === 'play.sooplive.com'
        ? 3
        : (hostname === 'youtube.com' || hostname === 'www.youtube.com' || hostname === 'm.youtube.com')
          ? 4
          : 0;
  if (!provider) return;

  const post = window.chrome.webview.postMessage.bind(window.chrome.webview);
  const startedAt = performance.now();
  const layoutDeadlineMs = 15000;
  const mutationDelayMs = 750;
  const periodicIntervalMs = 2000;
  const heartbeatIntervalMs = 2000;
  const maximumStatusNodes = 24;
  const maximumStatusTextLength = 12000;
  const maximumNodeTextLength = 2048;
  let lastMessage = '';
  let lastSentAt = Number.NEGATIVE_INFINITY;
  let scheduled = false;
  let observer = null;
  let observerConnected = false;

  const genericStatusSelectors = [
    '[data-chatview-state]',
    '[data-chatview-status]',
    '[role="alert"]',
    '[role="status"]'
  ];
  const providerStatusSelectors = {
    1: [
      '[data-testid*="status"]',
      '[class*="notice"]',
      '[class*="error"]',
      '[class*="empty"]',
      '[class*="login"]',
      '[class*="offline"]'
    ],
    2: [
      '[class*="notice"]',
      '[class*="error"]',
      '[class*="empty"]',
      '[class*="login"]',
      '[class*="offline"]',
      '[class*="ended"]'
    ],
    3: [
      '#chat_area [class*="notice"]',
      '#chat_area [class*="error"]',
      '#chat_area [class*="empty"]',
      '#chat_area [class*="login"]',
      '#chat_area [class*="offline"]',
      '#chat_area [class*="end"]'
    ],
    4: [
      'yt-live-chat-message-input-renderer',
      'yt-live-chat-viewer-engagement-message-renderer',
      'yt-live-chat-restricted-participation-renderer',
      'yt-live-chat-placeholder-item-renderer',
      'yt-live-chat-banner-renderer'
    ]
  };
  const readySelectors = {
    1: [
      '[data-chatview-ready="true"]',
      '[data-testid*="chat"]',
      '[class*="chat-list"]',
      '[class*="chat_list"]',
      '[class*="chatting-list"]',
      '[class*="message-list"]',
      'iframe[src*="chat"]'
    ],
    2: [
      '[class*="live_chatting_list"]',
      '[class*="chatting_list"]',
      '[class*="chat_list"]',
      '[data-testid*="chat"]',
      '[class*="chatting-area"]'
    ],
    3: [
      '#chat_area',
      '#chatting_list',
      '[class*="chatting-list"]',
      '[class*="chat_list"]',
      '[class*="chat-list"]',
      '[data-testid*="chat"]'
    ],
    4: [
      'yt-live-chat-renderer #items',
      'yt-live-chat-item-list-renderer',
      'yt-live-chat-renderer',
      'yt-live-chat-app'
    ]
  };
  const offlinePhrases = [
    '방송이 종료되었습니다',
    '라이브가 종료되었습니다',
    '방송이 종료되었어요',
    '방송 종료',
    'stream has ended',
    'live stream is offline',
    'this live stream has ended',
    'broadcast has ended'
  ];
  const loginPhrases = [
    '로그인이 필요합니다',
    '로그인 후 이용',
    '로그인해주세요',
    '로그인 해주세요',
    'sign in to chat',
    'log in to chat',
    'login required'
  ];
  const connectionPhrases = [
    '채팅 연결이 끊어졌습니다',
    '연결이 끊어졌습니다',
    '네트워크 연결이 불안정합니다',
    '재연결 중',
    'connection lost',
    'chat disconnected',
    'disconnected from chat',
    'reconnecting to chat',
    'unable to connect to chat',
    'failed to connect to chat'
  ];

  const isRendered = (element, minimumWidth = 1, minimumHeight = 1) => {
    if (!element || typeof element.getBoundingClientRect !== 'function') return false;
    const rect = element.getBoundingClientRect();
    if (rect.width < minimumWidth || rect.height < minimumHeight) return false;
    const style = getComputedStyle(element);
    return style.display !== 'none' &&
      style.visibility !== 'hidden' &&
      Number.parseFloat(style.opacity || '1') > 0.01;
  };

  const queryAll = (selector) => {
    try { return document.querySelectorAll(selector); } catch (_) { return []; }
  };

  const includesAny = (text, phrases) => {
    for (let index = 0; index < phrases.length; ++index) {
      if (text.includes(phrases[index])) return index + 1;
    }
    return 0;
  };

  const collectStatusText = () => {
    const parts = [];
    if (document.title) parts.push(document.title.slice(0, maximumNodeTextLength));

    const selectors = genericStatusSelectors.concat(
      providerStatusSelectors[provider] || []);
    let visited = 0;
    let length = parts.reduce((sum, part) => sum + part.length, 0);

    outer:
    for (const selector of selectors) {
      for (const element of queryAll(selector)) {
        if (visited >= maximumStatusNodes || length >= maximumStatusTextLength) break outer;
        ++visited;
        if (!isRendered(element)) continue;
        const text = String(element.textContent || '')
          .slice(0, maximumNodeTextLength)
          .trim();
        if (!text) continue;
        parts.push(text);
        length += text.length;
      }
    }
    return parts.join(' ').slice(0, maximumStatusTextLength).toLowerCase();
  };

  const explicitState = () => {
    let element = null;
    try { element = document.querySelector('[data-chatview-state]'); } catch (_) {}
    if (!element || typeof element.getAttribute !== 'function') return null;
    const value = String(element.getAttribute('data-chatview-state') || '')
      .trim()
      .toLowerCase();
    switch (value) {
      case 'loading': return [3, 100];
      case 'ready': return [4, 101];
      case 'login-required': return [7, 102];
      case 'offline': return [8, 103];
      case 'layout-changed': return [9, 104];
      case 'network-offline': return [10, 105];
      case 'connection-lost': return [11, 106];
      default: return null;
    }
  };

  function stopObserving() {
    if (observer && observerConnected) {
      observer.disconnect();
      observerConnected = false;
    }
  }

  function startObserving() {
    if (!observer) observer = new MutationObserver(schedule);
    if (!observerConnected && document.documentElement) {
      observer.observe(document.documentElement, { childList: true, subtree: true });
      observerConnected = true;
    }
  }

  const send = (state, detail = 0) => {
    const message = `CVH2|${provider}|${state}|${detail}`;
    const now = performance.now();
    if (message !== lastMessage ||
        now - lastSentAt >= heartbeatIntervalMs) {
      lastMessage = message;
      lastSentAt = now;
      try { post(message); } catch (_) {}
    }
    if (state === 3 || state === 11) startObserving();
    else stopObserving();
  };

  function evaluate() {
    if (typeof navigator !== 'undefined' && navigator.onLine === false) {
      send(10, 1);
      return;
    }

    if (!document.body || document.readyState === 'loading') {
      send(3, 0);
      return;
    }

    const explicit = explicitState();
    if (explicit) {
      send(explicit[0], explicit[1]);
      return;
    }

    const statusText = collectStatusText();
    const offlineDetail = includesAny(statusText, offlinePhrases);
    if (offlineDetail) {
      send(8, offlineDetail);
      return;
    }

    const loginDetail = includesAny(statusText, loginPhrases);
    if (loginDetail) {
      send(7, loginDetail);
      return;
    }

    const connectionDetail = includesAny(statusText, connectionPhrases);
    if (connectionDetail) {
      send(11, connectionDetail);
      return;
    }

    const candidates = readySelectors[provider] || [];
    for (let index = 0; index < candidates.length; ++index) {
      let element = null;
      try { element = document.querySelector(candidates[index]); } catch (_) {}
      if (isRendered(element, 80, 80)) {
        send(4, index + 1);
        return;
      }
    }

    if (performance.now() - startedAt >= layoutDeadlineMs) {
      send(9, 1);
    } else {
      send(3, 0);
    }
  }

  function schedule() {
    if (scheduled) return;
    scheduled = true;
    setTimeout(() => {
      scheduled = false;
      evaluate();
    }, mutationDelayMs);
  }

  const install = () => {
    if (!document.documentElement) {
      setTimeout(install, 0);
      return;
    }
    startObserving();
    evaluate();
  };

  window.addEventListener('offline', () => {
    send(10, 200);
  });
  window.addEventListener('online', () => {
    startObserving();
    send(11, 201);
    schedule();
  });
  document.addEventListener('DOMContentLoaded', schedule, { once: true });
  window.addEventListener('load', schedule, { once: true });
  setInterval(evaluate, periodicIntervalMs);
  install();
})();
)JS";

bool parse_unsigned(
    std::wstring_view field,
    std::uint32_t maximum,
    std::uint32_t &value) noexcept
{
    if (field.empty()) {
        return false;
    }

    std::uint32_t parsed = 0U;
    for (const wchar_t character : field) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(character - L'0');
        if (digit > maximum || parsed > (maximum - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }

    value = parsed;
    return true;
}

bool split_message(
    std::wstring_view message,
    std::array<std::wstring_view, 4U> &fields) noexcept
{
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index < fields.size(); ++index) {
        const std::size_t separator = message.find(L'|', begin);
        if (index + 1U == fields.size()) {
            if (separator != std::wstring_view::npos) {
                return false;
            }
            fields[index] = message.substr(begin);
            return true;
        }
        if (separator == std::wstring_view::npos) {
            return false;
        }
        fields[index] = message.substr(begin, separator - begin);
        begin = separator + 1U;
    }
    return false;
}

} // namespace

const wchar_t *page_health_bootstrap_script() noexcept
{
    return kPageHealthBootstrapScript;
}

HudProvider provider_for_chat_document(std::wstring_view url) noexcept
{
    if (url.starts_with(L"https://weflab.com/")) {
        return HudProvider::Weflab;
    }
    if (url.starts_with(L"https://chzzk.naver.com/") ||
        url.starts_with(L"https://m.chzzk.naver.com/")) {
        return HudProvider::Chzzk;
    }
    if (url.starts_with(L"https://play.sooplive.com/")) {
        return HudProvider::Soop;
    }
    if (url.starts_with(L"https://www.youtube.com/") ||
        url.starts_with(L"https://youtube.com/") ||
        url.starts_with(L"https://m.youtube.com/")) {
        return HudProvider::YouTube;
    }
    return HudProvider::Unknown;
}

bool is_dom_reportable_page_state(HudPageState state) noexcept
{
    switch (state) {
    case HudPageState::Loading:
    case HudPageState::Ready:
    case HudPageState::LoginRequired:
    case HudPageState::Offline:
    case HudPageState::LayoutChanged:
    case HudPageState::NetworkOffline:
    case HudPageState::ConnectionLost:
        return true;
    default:
        return false;
    }
}

bool parse_page_health_message(
    std::wstring_view message,
    HudHealthSnapshot &snapshot) noexcept
{
    if (message.empty() || message.size() > kMaximumMessageLength) {
        return false;
    }

    std::array<std::wstring_view, 4U> fields{};
    if (!split_message(message, fields) || fields[0] != kMessagePrefix) {
        return false;
    }

    std::uint32_t provider_value = 0U;
    std::uint32_t state_value = 0U;
    std::uint32_t detail_value = 0U;
    if (!parse_unsigned(
            fields[1],
            static_cast<std::uint32_t>(HudProvider::YouTube),
            provider_value) ||
        !parse_unsigned(
            fields[2],
            static_cast<std::uint32_t>(HudPageState::Fatal),
            state_value) ||
        !parse_unsigned(
            fields[3],
            std::numeric_limits<std::uint16_t>::max(),
            detail_value)) {
        return false;
    }

    const HudHealthSnapshot candidate{
        static_cast<HudPageState>(state_value),
        static_cast<HudProvider>(provider_value),
        static_cast<std::uint16_t>(detail_value)};
    if (candidate.provider == HudProvider::Unknown ||
        !is_valid_hud_health(candidate) ||
        !is_dom_reportable_page_state(candidate.state)) {
        return false;
    }

    snapshot = candidate;
    return true;
}

} // namespace chatview
