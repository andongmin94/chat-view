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
    inPostingInput = false,
  } = {}) {
    this.rect = { width, height };
    this.style = { display, visibility, opacity };
    this.textContent = textContent;
    this.dataState = dataState;
    this.inPostingInput = inPostingInput;
  }

  closest(selector) {
    return selector === 'yt-live-chat-message-input-renderer' && this.inPostingInput
      ? this
      : null;
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
  navigatorOnline = true,
}) {
  const messages = [];
  const observers = [];
  const intervals = [];
  const events = new Map();
  const navigator = { onLine: navigatorOnline };
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
    addEventListener(type, callback) {
      events.set(type, callback);
    },
  };
  window.top = window;

  const context = vm.createContext({
    document,
    getComputedStyle(element) {
      return element.style;
    },
    location: { hostname },
    MutationObserver: FakeMutationObserver,
    navigator,
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
  return { messages, observers, intervals, events, navigator };
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
    [`CVH2|${provider}|4|1`],
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

const networkOffline = runProbe({
  hostname: 'www.youtube.com',
  navigatorOnline: false,
  selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  nowValues: [0, 0, 100, 101, 200],
});
assert.deepEqual(
  networkOffline.messages,
  ['CVH2|4|10|1'],
  'navigator offline state did not report NetworkOffline',
);
networkOffline.navigator.onLine = true;
networkOffline.events.get('online')();
assert.deepEqual(
  networkOffline.messages,
  ['CVH2|4|10|1', 'CVH2|4|11|201', 'CVH2|4|4|1'],
  'returning online did not report recovery and immediately re-evaluate chat',
);
networkOffline.navigator.onLine = false;
networkOffline.events.get('offline')();
assert.deepEqual(
  networkOffline.messages,
  ['CVH2|4|10|1', 'CVH2|4|11|201', 'CVH2|4|4|1', 'CVH2|4|10|200'],
  'an offline event did not immediately report NetworkOffline',
);

const loading = runProbe({
  hostname: 'www.youtube.com',
  readyState: 'loading',
});
assert.deepEqual(
  loading.messages,
  ['CVH2|4|3|0'],
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
  ['CVH2|2|8|1'],
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
  ['CVH2|4|7|5'],
  'a YouTube sign-in prompt did not report LoginRequired',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    selectors: new Map([[
      'yt-live-chat-banner-renderer',
      new FakeElement({ textContent: 'Reconnecting to chat' }),
    ]]),
  }).messages,
  ['CVH2|4|11|8'],
  'a provider reconnecting banner did not report ConnectionLost',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    bodyText: 'A viewer wrote: sign in to chat',
    selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  }).messages,
  ['CVH2|4|4|1'],
  'ordinary chat message text was incorrectly treated as a login prompt',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    bodyText: 'A viewer wrote: reconnecting to chat',
    selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  }).messages,
  ['CVH2|4|4|1'],
  'ordinary chat message text was incorrectly treated as a connection failure',
);

const explicitReady = new FakeElement({ dataState: 'ready' });
assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    selectors: new Map([['[data-chatview-state]', explicitReady]]),
  }).messages,
  ['CVH2|1|4|101'],
  'an explicit Weflab ready state was ignored',
);

const explicitOffline = new FakeElement({ dataState: 'offline' });
assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    selectors: new Map([['[data-chatview-state]', explicitOffline]]),
  }).messages,
  ['CVH2|1|8|103'],
  'an explicit Weflab offline state was ignored',
);

assert.deepEqual(
  runProbe({
    hostname: 'weflab.com',
    nowValues: [0, 16000],
  }).messages,
  ['CVH2|1|9|1'],
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
  ['CVH2|3|3|0'],
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
  ['CVH2|2|3|0'],
  'a hidden status banner was incorrectly reported as an active page state',
);

// Repeated unchanged messages are intentional heartbeats for the native watchdog.
const heartbeats = runProbe({
  hostname: 'www.youtube.com',
  selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
  nowValues: [0, 0, 1999, 2000],
});
assert.equal(heartbeats.intervals.length, 1, 'the periodic health check was not installed');
heartbeats.intervals[0]();
assert.deepEqual(
  heartbeats.messages,
  ['CVH2|4|4|1'],
  'unchanged state emitted before the heartbeat interval elapsed',
);
heartbeats.intervals[0]();
assert.deepEqual(
  heartbeats.messages,
  ['CVH2|4|4|1', 'CVH2|4|4|1'],
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
  ['CVH2|4|4|1', 'CVH2|4|7|5'],
  'a changed page state waited for the heartbeat interval before reporting',
);

// A posting-only sign-in prompt must not override a visible message list.
const postingPrompt = new FakeElement({
  textContent: 'Sign in to chat',
  inPostingInput: true,
});
for (const readerSelector of [
  'yt-live-chat-renderer #items',
  'yt-live-chat-item-list-renderer',
]) {
  const selectors = new Map([
    [readerSelector, new FakeElement()],
    ['yt-live-chat-message-input-renderer', postingPrompt],
    // The same input may also match a generic status selector.
    ['[role="status"]', postingPrompt],
  ]);
  const expectedDetail = readerSelector.endsWith('#items') ? 1 : 2;
  assert.deepEqual(
    runProbe({ hostname: 'www.youtube.com', selectors }).messages,
    [`CVH2|4|4|${expectedDetail}`],
    'posting permission incorrectly blocked readable YouTube chat',
  );

  for (const [text, expected] of [
    ['Login required', 'CVH2|4|7|7'],
    ['Reconnecting to chat', 'CVH2|4|11|8'],
    ['This live stream has ended', 'CVH2|4|8|5'],
  ]) {
    const withBlocker = new Map(selectors);
    withBlocker.set('[role="alert"]', new FakeElement({ textContent: text }));
    assert.deepEqual(
      runProbe({ hostname: 'www.youtube.com', selectors: withBlocker }).messages,
      [expected],
      'a real reader-level blocker was masked by posting-only prompt handling',
    );
  }
}

for (const reader of [
  null,
  new FakeElement({ display: 'none' }),
  new FakeElement({ width: 40, height: 40 }),
]) {
  assert.deepEqual(
    runProbe({
      hostname: 'www.youtube.com',
      selectors: new Map([
        ['yt-live-chat-renderer #items', reader],
        ['yt-live-chat-app', new FakeElement()],
        ['yt-live-chat-message-input-renderer', postingPrompt],
      ]),
    }).messages,
    ['CVH2|4|7|5'],
    'a shell without a visible reader incorrectly bypassed the login state',
  );
}

console.log('Embedded page-health script tests passed.');
