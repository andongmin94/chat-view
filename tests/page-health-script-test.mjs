// SPDX-License-Identifier: GPL-2.0-or-later

import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync('src/hud/page-health-message.cpp', 'utf8');
const match = source.match(/LR"JS\(([\s\S]*?)\)JS"/u);
assert.ok(match, 'page-health JavaScript raw string was not found');
const script = match[1];
new vm.Script(script, { filename: 'page-health-message.js' });

class FakeElement {
  constructor({
    width = 320,
    height = 240,
    display = 'block',
    visibility = 'visible',
    opacity = '1',
    textContent = '',
    dataState = null,
  } = {}) {
    this.rect = { width, height };
    this.style = { display, visibility, opacity };
    this.textContent = textContent;
    this.dataState = dataState;
  }

  getBoundingClientRect() {
    return this.rect;
  }

  getAttribute(name) {
    return name === 'data-chatview-state' ? this.dataState : null;
  }
}

function asElements(value) {
  if (value == null) return [];
  return Array.isArray(value) ? value : [value];
}

function runProbe({
  hostname,
  readyState = 'complete',
  bodyText = '',
  title = '',
  selectors = new Map(),
  nowValues = [0, 0],
}) {
  const messages = [];
  const observers = [];
  const intervals = [];
  let nowIndex = 0;

  const body = {
    innerText: bodyText,
    textContent: bodyText,
  };
  const documentElement = {};
  const document = {
    body,
    documentElement,
    readyState,
    title,
    addEventListener() {},
    querySelector(selector) {
      return asElements(selectors.get(selector))[0] ?? null;
    },
    querySelectorAll(selector) {
      return asElements(selectors.get(selector));
    },
  };

  class FakeMutationObserver {
    constructor(callback) {
      this.callback = callback;
      this.connected = false;
      observers.push(this);
    }

    observe() {
      this.connected = true;
    }

    disconnect() {
      this.connected = false;
    }

    trigger() {
      if (this.connected) {
        this.callback([], this);
      }
    }
  }

  const webview = {
    postMessage(message) {
      messages.push(message);
    },
  };
  const window = {
    chrome: { webview },
    addEventListener() {},
  };
  window.top = window;

  const context = vm.createContext({
    document,
    getComputedStyle(element) {
      return element.style;
    },
    location: { hostname },
    MutationObserver: FakeMutationObserver,
    Number,
    performance: {
      now() {
        const index = Math.min(nowIndex, nowValues.length - 1);
        nowIndex += 1;
        return nowValues[index];
      },
    },
    setInterval(callback) {
      intervals.push(callback);
      return intervals.length;
    },
    setTimeout(callback) {
      callback();
      return 1;
    },
    String,
    window,
  });

  vm.runInContext(script, context, { filename: 'page-health-message.js' });
  return { messages, observers, intervals };
}

const providerCases = [
  ['weflab.com', '[data-chatview-ready="true"]', 1],
  ['chzzk.naver.com', '[class*="live_chatting_list"]', 2],
  ['m.chzzk.naver.com', '[class*="live_chatting_list"]', 2],
  ['play.sooplive.com', '#chat_area', 3],
  ['www.youtube.com', 'yt-live-chat-renderer #items', 4],
  ['youtube.com', 'yt-live-chat-renderer #items', 4],
  ['m.youtube.com', 'yt-live-chat-renderer #items', 4],
];

for (const [hostname, selector, provider] of providerCases) {
  const result = runProbe({
    hostname,
    selectors: new Map([[selector, new FakeElement()]]),
  });
  assert.deepEqual(
    result.messages,
    [`CVH1|${provider}|4|1`],
    `${hostname} did not report the expected ready state`,
  );
  assert.equal(
    result.observers[0].connected,
    false,
    `${hostname} kept observing the continuously mutating chat DOM after becoming ready`,
  );
}

assert.deepEqual(
  runProbe({ hostname: 'example.com' }).messages,
  [],
  'an unsupported host emitted page-health telemetry',
);

const loading = runProbe({
  hostname: 'www.youtube.com',
  readyState: 'loading',
});
assert.deepEqual(
  loading.messages,
  ['CVH1|4|3|0'],
  'a loading document did not report Loading',
);
assert.equal(
  loading.observers[0].connected,
  true,
  'a loading document stopped observing before its chat surface appeared',
);

assert.deepEqual(
  runProbe({
    hostname: 'chzzk.naver.com',
    selectors: new Map([[
      '[class*="notice"]',
      new FakeElement({ textContent: '방송이 종료되었습니다' }),
    ]]),
  }).messages,
  ['CVH1|2|8|1'],
  'an ended CHZZK broadcast did not report Offline',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    selectors: new Map([[
      'yt-live-chat-message-input-renderer',
      new FakeElement({ textContent: 'Sign in to chat' }),
    ]]),
  }).messages,
  ['CVH1|4|7|5'],
  'a YouTube sign-in prompt did not report LoginRequired',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    bodyText: 'A viewer wrote: sign in to chat',
    selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  }).messages,
  ['CVH1|4|4|1'],
  'ordinary chat message text was incorrectly treated as a login prompt',
);

const explicitReady = new FakeElement({ dataState: 'ready' });
assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    selectors: new Map([['[data-chatview-state]', explicitReady]]),
  }).messages,
  ['CVH1|1|4|101'],
  'an explicit Weflab ready state was ignored',
);

const explicitOffline = new FakeElement({ dataState: 'offline' });
assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    selectors: new Map([['[data-chatview-state]', explicitOffline]]),
  }).messages,
  ['CVH1|1|8|103'],
  'an explicit Weflab offline state was ignored',
);

assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    nowValues: [0, 16000],
  }).messages,
  ['CVH1|1|9|1'],
  'a page without a recognized chat layout did not report LayoutChanged',
);

assert.deepEqual(
  runProbe({
    hostname: 'play.sooplive.com',
    selectors: new Map([[
      '#chat_area',
      new FakeElement({ width: 40, height: 40 }),
    ]]),
  }).messages,
  ['CVH1|3|3|0'],
  'an undersized SOOP element was incorrectly accepted as a ready chat surface',
);

assert.deepEqual(
  runProbe({
    hostname: 'chzzk.naver.com',
    selectors: new Map([[
      '[class*="notice"]',
      new FakeElement({
        textContent: '방송이 종료되었습니다',
        display: 'none',
      }),
    ]]),
  }).messages,
  ['CVH1|2|3|0'],
  'a hidden status banner was incorrectly reported as an active page state',
);

const heartbeats = runProbe({
  hostname: 'www.youtube.com',
  selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  nowValues: [0, 0, 1999, 2000],
});
assert.equal(heartbeats.intervals.length, 1, 'the periodic health check was not installed');
heartbeats.intervals[0]();
assert.deepEqual(
  heartbeats.messages,
  ['CVH1|4|4|1'],
  'unchanged state emitted before the heartbeat interval elapsed',
);
heartbeats.intervals[0]();
assert.deepEqual(
  heartbeats.messages,
  ['CVH1|4|4|1', 'CVH1|4|4|1'],
  'an unchanged ready page did not emit its periodic heartbeat',
);

const changingSelectors = new Map([
  ['yt-live-chat-renderer #items', new FakeElement()],
]);
const changedState = runProbe({
  hostname: 'www.youtube.com',
  selectors: changingSelectors,
  nowValues: [0, 0, 100],
});
changingSelectors.clear();
changingSelectors.set(
  'yt-live-chat-message-input-renderer',
  new FakeElement({ textContent: 'Sign in to chat' }),
);
changedState.intervals[0]();
assert.deepEqual(
  changedState.messages,
  ['CVH1|4|4|1', 'CVH1|4|7|5'],
  'a changed page state waited for the heartbeat interval before reporting',
);

console.log('Embedded page-health script tests passed.');
