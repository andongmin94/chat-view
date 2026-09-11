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
  constructor({ width = 320, height = 240, display = 'block', visibility = 'visible', opacity = '1' } = {}) {
    this.rect = { width, height };
    this.style = { display, visibility, opacity };
  }

  getBoundingClientRect() {
    return this.rect;
  }
}

function runProbe({
  hostname,
  readyState = 'complete',
  bodyText = '',
  selectors = new Map(),
  nowValues = [0, 0],
}) {
  const messages = [];
  const observers = [];
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
    addEventListener() {},
    querySelector(selector) {
      return selectors.get(selector) ?? null;
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
    setInterval() {
      return 1;
    },
    setTimeout(callback) {
      callback();
      return 1;
    },
    window,
  });

  vm.runInContext(script, context, { filename: 'page-health-message.js' });
  return { messages, observers };
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
}

assert.deepEqual(
  runProbe({ hostname: 'example.com' }).messages,
  [],
  'an unsupported host emitted page-health telemetry',
);

assert.deepEqual(
  runProbe({ hostname: 'www.youtube.com', readyState: 'loading' }).messages,
  ['CVH1|4|3|0'],
  'a loading document did not report Loading',
);

assert.deepEqual(
  runProbe({
    hostname: 'chzzk.naver.com',
    bodyText: '방송이 종료되었습니다',
  }).messages,
  ['CVH1|2|8|1'],
  'an ended CHZZK broadcast did not report Offline',
);

assert.deepEqual(
  runProbe({
    hostname: 'www.youtube.com',
    bodyText: 'Sign in to chat',
  }).messages,
  ['CVH1|4|7|5'],
  'a YouTube sign-in prompt did not report LoginRequired',
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

const deduplicated = runProbe({
  hostname: 'www.youtube.com',
  selectors: new Map([['yt-live-chat-renderer #items', new FakeElement()]]),
});
assert.equal(deduplicated.observers.length, 1, 'the health observer was not installed');
deduplicated.observers[0].trigger();
assert.deepEqual(
  deduplicated.messages,
  ['CVH1|4|4|1'],
  'unchanged health telemetry was emitted more than once',
);

console.log('Embedded page-health script tests passed.');
