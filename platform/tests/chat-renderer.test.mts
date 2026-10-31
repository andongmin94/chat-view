// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { renderChat } from '../web/chat-renderer.js';

class Element {
  readonly tag: string;
  children: Element[] = [];
  parent: Element | null = null;
  textContent = '';
  className = '';
  dir = '';
  scrollTop = 0;
  clientHeight = 200;
  clientWidth = 480;
  wrappedHeight = 0;
  replaced = 0;
  added = 0;
  removed = 0;
  browserScrollAnchoring = false;
  hidden = true;
  disabled = false;
  readonly style = { fontSize: '' };
  private readonly classes = new Set<string>();
  readonly classList = {
    toggle: (name: string, enabled: boolean) => enabled ? this.classes.add(name) : this.classes.delete(name),
    contains: (name: string) => this.classes.has(name),
  };
  private readonly attributes = new Map<string, string>();
  setAttribute(name: string, value: string) { this.attributes.set(name, value); }
  getAttribute(name: string) { return this.attributes.get(name) ?? null; }
  private readonly listeners = new Map<string, () => void>();
  constructor(tag: string) { this.tag = tag; }
  addEventListener(name: string, listener: () => void) { this.listeners.set(name, listener); }
  fire(name: string) { this.listeners.get(name)?.(); }
  set innerHTML(_value: string) { throw new Error('HTML injection'); }
  get rowHeight() {
    return 30 + this.wrappedHeight + ((Number.parseInt(this.style.fontSize || '16', 10) - 16) * 3) +
      (this.classList.contains('chat-high-contrast') ? 4 : 0);
  }
  get scrollHeight() { return this.children.length * (this.tag === 'ol' ? this.rowHeight : 30); }
  get firstChild() { return this.children[0]; }
  getBoundingClientRect() {
    if (this.parent?.tag === 'ol') {
      const height = this.parent.rowHeight;
      const top = this.parent.children.indexOf(this) * height - this.parent.scrollTop;
      return { top, bottom: top + height };
    }
    return { top: 0, bottom: this.clientHeight };
  }
  append(...children: Element[]) {
    for (const child of children) { child.parent = this; this.children.push(child); }
  }
  appendChild(child: Element) { this.added++; child.parent = this; this.children.push(child); }
  removeChild(child: Element) {
    assert.equal(this.firstChild, child);
    this.removed++;
    // Model Chromium's automatic scroll anchoring for rows above the viewport.
    if (this.browserScrollAnchoring && this.tag === 'ol' && this.scrollTop >= this.rowHeight)
      this.scrollTop -= this.rowHeight;
    this.children.shift(); child.parent = null;
  }
  replaceChildren(...children: Element[]) {
    this.replaced++;
    for (const child of this.children) child.parent = null;
    this.children = children;
    for (const child of children) child.parent = this;
  }
}
function fixture(prefs = new Map<string, string>()) {
  const list = new Element('ol'), status = new Element('p'), jump = new Element('button');
  const smaller = new Element('button'), larger = new Element('button'), contrast = new Element('button');
  const controls = { 'chat-font-smaller': smaller, 'chat-font-larger': larger, 'chat-contrast': contrast };
  const resizeCallbacks: (() => void)[] = [];
  class ResizeObserver {
    constructor(callback: () => void) { resizeCallbacks.push(callback); }
    observe(target: Element) { assert.equal(target, list); }
  }
  const document = {
    getElementById: (id: string) =>
      id === 'status' ? status : id === 'messages' ? list : id === 'jump-latest' ? jump :
      controls[id as keyof typeof controls] ?? null,
    createElement: (tag: string) => new Element(tag),
    defaultView: { ResizeObserver, localStorage: {
      getItem: (name: string) => prefs.get(name) ?? null,
      setItem: (name: string, value: string) => { prefs.set(name, value); },
    } },
  };
  return { list, status, jump, smaller, larger, contrast, prefs, document,
    resizeCallbacks, resize: () => resizeCallbacks.forEach(callback => callback()) };
}
const message = (n: number) => ({
  nickname: 'viewer <script>', content: `채팅 ${n} 😀 <img onerror=evil()>`, messageTime: 1700000000000 + n,
});
const frame = (received: number, messages: ReturnType<typeof message>[], state = 'subscribed') =>
  ({ state, received, messages });

test('five-second display heartbeat acknowledges the same chat without rebuilding rows or scrolling', () => {
  const { list, status, document } = fixture();
  renderChat(document, frame(3, [message(1), message(2), message(3)]));
  const first = list.children[0], second = list.children[1];
  list.scrollTop = 17;
  for (let i = 0; i < 5; i++) {
    renderChat(document, frame(3, [message(1), message(2), message(3)]));
    assert.equal(list.children[0], first);
    assert.equal(list.children[1], second);
  }
  assert.equal(list.replaced, 1);
  assert.equal(list.added, 0);
  assert.equal(list.removed, 0);
  assert.equal(list.scrollTop, 17);
  assert.match(status.textContent, /메시지 3개/u);
});

test('new chat appends only missing rows, preserving existing 100-row history and reader position', () => {
  const { list, document } = fixture();
  list.clientHeight = 100;
  renderChat(document, frame(100, Array.from({ length: 100 }, (_, i) => message(i + 1))));
  const retained = list.children[1], dropped = list.children[0];
  list.scrollTop = 400;
  renderChat(document, frame(101, Array.from({ length: 100 }, (_, i) => message(i + 2))));
  assert.equal(list.children.length, 100);
  assert.equal(list.children[0], retained);
  assert.ok(!list.children.includes(dropped));
  assert.equal(list.replaced, 1);
  assert.equal(list.removed, 1);
  assert.equal(list.added, 1);
  assert.equal(list.scrollTop, 370, 'evicting one old row preserves the surviving row at the same visual offset');
  assert.equal(list.children.at(-1)?.children[2]?.textContent, message(101).content);
});

test('rolling history does not double-adjust when Chromium already anchors the visible chat', () => {
  const { list, jump, document } = fixture();
  list.clientHeight = 100;
  list.browserScrollAnchoring = true;
  renderChat(document, frame(100, Array.from({ length: 100 }, (_, i) => message(i + 1))));
  const surviving = list.children[13]!;
  list.scrollTop = 405;
  const top = surviving.getBoundingClientRect().top;
  renderChat(document, frame(101, Array.from({ length: 100 }, (_, i) => message(i + 2))));
  assert.equal(list.children[12], surviving, 'reuse the actual old message row');
  assert.equal(surviving.getBoundingClientRect().top, top,
    'a browser-adjusted scroll must not be shifted by the removed row again');
  assert.equal(list.scrollTop, 375);
  assert.equal(jump.textContent, '새 채팅 1개 · 최신으로');
  assert.equal(list.replaced, 1, 'rolling history keeps existing DOM nodes');
});

test('batched deliveries append without rebuilding; missing 100+ messages safely reset bounded history', () => {
  const { list, document } = fixture();
  renderChat(document, frame(2, [message(1), message(2)]));
  const existing = list.children[0];
  renderChat(document, frame(5, [message(1), message(2), message(3), message(4), message(5)]));
  assert.equal(list.children[0], existing);
  assert.equal(list.added, 3);
  assert.equal(list.replaced, 1);
  renderChat(document, frame(125, Array.from({ length: 100 }, (_, i) => message(i + 26))));
  assert.equal(list.children.length, 100);
  assert.equal(list.replaced, 2);
  assert.equal(list.children[0]?.children[2]?.textContent, message(26).content);
});

test('disconnect, revoke and same-count changed text retire old DOM without executable markup', () => {
  const { list, status, document } = fixture();
  renderChat(document, frame(1, [message(1)]));
  const old = list.children[0];
  renderChat(document, frame(0, [], 'disconnected'));
  assert.equal(list.children.length, 0);
  assert.match(status.textContent, /끊겼/u);
  renderChat(document, frame(1, [message(1)]));
  assert.equal(list.children.length, 1);
  assert.notEqual(list.children[0], old, 'fresh session must not reuse old session DOM');
  const newFirst = list.children[0];
  renderChat(document, frame(1, [{ ...message(1), content: '수정된 다른 내용' }]));
  assert.notEqual(list.children[0], newFirst, 'same received count is not proof that content is unchanged');
  assert.equal(list.children[0]?.children[2]?.textContent, '수정된 다른 내용');
  renderChat(document, frame(1, [message(1)], 'revoked'));
  assert.equal(list.children.length, 0);
  assert.match(status.textContent, /철회/u);
  assert.equal(list.children.some(node => node.tag === 'script'), false);
});

test('scrolled-up streamer sees unread messages, unchanged heartbeats do not duplicate them, and latest jump clears', () => {
  const { list, jump, document } = fixture();
  renderChat(document, frame(10, Array.from({ length: 10 }, (_, i) => message(i + 1))));
  list.scrollTop = 0;
  renderChat(document, frame(12, Array.from({ length: 12 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, false);
  assert.equal(jump.textContent, '새 채팅 2개 · 최신으로');
  assert.equal(list.scrollTop, 0, 'do not interrupt a reader looking at old chat');
  renderChat(document, frame(12, Array.from({ length: 12 }, (_, i) => message(i + 1))));
  assert.equal(jump.textContent, '새 채팅 2개 · 최신으로', 'a five-second heartbeat is not a new chat');
  renderChat(document, frame(13, Array.from({ length: 13 }, (_, i) => message(i + 1))));
  assert.equal(jump.textContent, '새 채팅 3개 · 최신으로');
  jump.fire('click');
  assert.equal(jump.hidden, true);
  assert.equal(list.scrollTop, list.scrollHeight, 'one click returns to actual latest text');
  renderChat(document, frame(14, Array.from({ length: 14 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, true, 'following live chat does not accumulate unread');
});

test('manual scroll to the bottom clears unread; disconnect and revoked state retire the local badge', () => {
  const { list, jump, document } = fixture();
  renderChat(document, frame(10, Array.from({ length: 10 }, (_, i) => message(i + 1))));
  list.scrollTop = 0;
  renderChat(document, frame(11, Array.from({ length: 11 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, false);
  list.scrollTop = list.scrollHeight;
  list.fire('scroll');
  assert.equal(jump.hidden, true);
  list.scrollTop = 0;
  renderChat(document, frame(12, Array.from({ length: 12 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, false);
  renderChat(document, frame(0, [], 'disconnected'));
  assert.equal(jump.hidden, true);
  renderChat(document, frame(12, Array.from({ length: 12 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, true, 'a resumed session starts without stale unread');
  list.scrollTop = 0;
  renderChat(document, frame(13, Array.from({ length: 13 }, (_, i) => message(i + 1))));
  assert.equal(jump.hidden, false);
  renderChat(document, frame(13, [], 'revoked'));
  assert.equal(jump.hidden, true);
  assert.equal(list.children.length, 0);
});

test('a large unread burst is bounded, and stale session counters reset the hint', () => {
  const { list, jump, document } = fixture();
  renderChat(document, frame(100, Array.from({ length: 100 }, (_, i) => message(i + 1))));
  list.scrollTop = 0;
  renderChat(document, frame(300, Array.from({ length: 100 }, (_, i) => message(i + 201))));
  assert.equal(jump.textContent, '새 채팅 99+개 · 최신으로');
  renderChat(document, frame(1, [message(1)]));
  assert.equal(jump.hidden, true, 'a new provider sequence must not inherit the old unread count');
  assert.equal(list.children.length, 1);
});

test('official streamer, channel and chat-manager labels plus verification render as inert text', () => {
  const { list, document } = fixture();
  const roles = [
    { code: 'streamer', label: '스트리머' },
    { code: 'streaming_channel_manager', label: '채널 관리자' },
    { code: 'streaming_chat_manager', label: '채팅 운영자' },
  ];
  const messages = roles.map(({code}, i) => ({
    ...message(i + 1), userRoleCode: code, verifiedMark: i === 0,
  }));
  renderChat(document, frame(3, messages));
  for (let i = 0; i < roles.length; i++) {
    const row = list.children[i]!;
    assert.equal(row.children[1]?.textContent, message(i + 1).nickname);
    assert.equal(row.children.at(-1)?.textContent, message(i + 1).content);
    const badge = row.children.find(el => el.className.includes('chat-role'));
    assert.equal(badge?.textContent, roles[i]!.label);
    assert(badge?.className.includes(roles[i]!.code), 'fixed class for official role only');
  }
  assert.equal(list.children[0]?.children.find(el => el.className === 'chat-verified')?.textContent, '인증');
  assert.equal(list.children[1]?.children.some(el => el.className === 'chat-verified'), false);
  assert.equal(list.children[0]?.children.some(el => el.tag === 'img' || el.tag === 'script'), false);

  const sameRow = list.children[0];
  renderChat(document, frame(3, messages));
  assert.equal(list.children[0], sameRow, 'heartbeat does not rebuild role badges');
  renderChat(document, frame(3, [{ ...messages[0]!, userRoleCode: 'common_user', verifiedMark: false }, ...messages.slice(1)]));
  assert.notEqual(list.children[0], sameRow, 'changed role with unchanged count and text invalidates stale badge');
  assert.equal(list.children[0]?.children.some(el => el.className.includes('chat-role')), false);
  assert.equal(list.children[0]?.children.some(el => el.className === 'chat-verified'), false);
});

test('unknown role strings, forged badge objects and profile markup cannot become visual authority', () => {
  const { list, document } = fixture();
  renderChat(document, frame(1, [{
    ...message(1), userRoleCode: 'streamer\"><img onerror=evil()>',
    verifiedMark: 'true', badges: [{ imageUrl: 'https://example.invalid/tracker' }],
    profile: { nickname: 'forged' },
  } as any]));
  assert.equal(list.children.length, 1);
  assert.deepEqual(list.children[0]?.children.map(el => el.tag), ['time', 'b', 'span']);
  assert.equal(list.children[0]?.children[2]?.textContent, message(1).content);
});

test('changing font size or contrast preserves the first visible message while reading old chat', () => {
  const { document, list, jump, larger, smaller, contrast } = fixture();
  list.clientHeight = 100;
  renderChat(document, frame(12, Array.from({ length: 12 }, (_, i) => message(i + 1))));
  const anchor = list.children[3]!;
  const oldRow = list.children[4]!;
  list.scrollTop = 115; // Midway through the fourth line; not at latest.
  const screenTop = anchor.getBoundingClientRect().top;
  assert.equal(screenTop, -25);
  renderChat(document, frame(13, Array.from({ length: 13 }, (_, i) => message(i + 1))));
  assert.equal(jump.textContent, '새 채팅 1개 · 최신으로');
  assert.equal(jump.hidden, false);

  larger.fire('click');
  assert.equal(list.style.fontSize, '18px');
  assert.equal(anchor.getBoundingClientRect().top, screenTop,
    'growing text does not shift the top visible historical message');
  assert.equal(list.children[4], oldRow, 'font change cannot rebuild received rows');
  contrast.fire('click');
  assert.equal(anchor.getBoundingClientRect().top, screenTop,
    'adding high contrast borders preserves the same historical row');
  smaller.fire('click');
  assert.equal(anchor.getBoundingClientRect().top, screenTop,
    'returning to smaller text preserves the reading position');
  assert.equal(jump.hidden, false);
  assert.equal(jump.textContent, '새 채팅 1개 · 최신으로',
    'readability changes do not acknowledge unread chat messages');
  jump.fire('click');
  assert.equal(jump.hidden, true);
  larger.fire('click');
  assert.equal(list.scrollTop, list.scrollHeight,
    'font adjustment follows newest content only when streamer is already at latest');
});

test('streamer can increase font, toggle contrast and retain only bounded local display preferences', () => {
  const prefs = new Map<string, string>();
  const { list, smaller, larger, contrast, document } = fixture(prefs);
  renderChat(document, frame(1, [message(1)]));
  const existing = list.children[0];
  assert.equal(list.style.fontSize, '16px');
  assert.equal(contrast.getAttribute('aria-pressed'), 'false');
  larger.fire('click');
  assert.equal(list.style.fontSize, '18px');
  contrast.fire('click');
  assert.equal(list.classList.contains('chat-high-contrast'), true);
  assert.equal(contrast.getAttribute('aria-pressed'), 'true');
  assert.equal(contrast.textContent, '기본 대비');
  assert.equal(list.children[0], existing, 'reading preferences must not recreate the chat text');
  for (let i = 0; i < 10; i++) larger.fire('click');
  assert.equal(list.style.fontSize, '24px');
  assert.equal(larger.disabled, true, 'large text is explicitly bounded');
  smaller.fire('click');
  assert.equal(list.style.fontSize, '22px');
  assert.deepEqual([...prefs.entries()].sort(), [
    ['chatview.font-size', '22'], ['chatview.high-contrast', '1'],
  ]);
  const second = fixture(prefs);
  renderChat(second.document, frame(1, [message(1)]));
  assert.equal(second.list.style.fontSize, '22px', 'bounded preference survives a new owned document');
  assert.equal(second.list.classList.contains('chat-high-contrast'), true);
  second.contrast.fire('click');
  assert.equal(second.list.classList.contains('chat-high-contrast'), false);
});

test('invalid stored styling and blocked storage cannot inject CSS or affect chat delivery', () => {
  const { list, smaller, document } = fixture(new Map([
    ['chatview.font-size', '9999px!important'], ['chatview.high-contrast', '<script>'],
  ]));
  renderChat(document, frame(1, [message(1)]));
  assert.equal(list.style.fontSize, '16px');
  assert.equal(list.classList.contains('chat-high-contrast'), false);
  document.defaultView = { get localStorage(): never { throw new Error('storage forbidden'); } } as any;
  smaller.fire('click');
  assert.equal(list.style.fontSize, '14px', 'bounded preference still works when persistence is blocked');
  assert.equal(smaller.disabled, true);
  renderChat(document, frame(0, [], 'revoked'));
  assert.equal(list.children.length, 0);
});


test('shrinking the HUD keeps live chat at the bottom before the next delivery', () => {
  const { document, list, jump, resize, resizeCallbacks } = fixture();
  const messages = Array.from({ length: 100 }, (_, i) => message(i + 1));
  renderChat(document, frame(100, messages));
  // Real browsers clamp a bottom assignment to scrollHeight - clientHeight.
  list.scrollTop = list.scrollHeight - list.clientHeight;
  list.fire('scroll');
  const rows = [...list.children];
  list.clientHeight = 80;
  resize();
  assert.equal(list.scrollTop, list.scrollHeight, 'resize alone follows latest, no heartbeat needed');
  assert.equal(jump.hidden, true);
  renderChat(document, frame(101, [...messages.slice(1), message(101)]));
  assert.equal(jump.hidden, true, 'the next live message must not become falsely unread');
  assert.equal(list.children[0], rows[1]);
  assert.equal(list.replaced, 1);
  assert.equal(resizeCallbacks.length, 1, 'one observer for this document, not one per frame');
});

test('width reflow preserves a historical row and unread count with or without browser anchoring', () => {
  for (const browserAnchored of [false, true]) {
    const { document, list, jump, resize } = fixture();
    renderChat(document, frame(30, Array.from({ length: 30 }, (_, i) => message(i + 1))));
    list.scrollTop = 305;
    list.fire('scroll');
    const anchor = list.children[10]!;
    const oldTop = anchor.getBoundingClientRect().top;
    renderChat(document, frame(31, Array.from({ length: 31 }, (_, i) => message(i + 1))));
    list.clientWidth = 240;
    list.wrappedHeight = 30;
    if (browserAnchored) list.scrollTop += 300;
    resize();
    assert.equal(anchor.getBoundingClientRect().top, oldTop);
    assert.equal(list.children[10], anchor);
    assert.equal(jump.textContent, '새 채팅 1개 · 최신으로');
    assert.equal(jump.hidden, false);
    assert.equal(list.replaced, 1);
  }
});

test('a heartbeat or new delivery before the resize callback preserves the old live-follow intent', () => {
  for (const deliver of [false, true]) {
    const { document, list, jump, resize } = fixture();
    const messages = Array.from({ length: 30 }, (_, i) => message(i + 1));
    renderChat(document, frame(30, messages));
    list.scrollTop = list.scrollHeight - list.clientHeight;
    list.fire('scroll');
    list.clientHeight = 70;
    renderChat(document, deliver ? frame(31, [...messages, message(31)]) : frame(30, messages));
    assert.equal(list.scrollTop, list.scrollHeight);
    resize();
    assert.equal(jump.hidden, true);
    assert.equal(list.replaced, 1);
  }
});

test('a resize-induced scroll before observer delivery does not replace the historical anchor', () => {
  const { document, list, resize } = fixture();
  renderChat(document, frame(30, Array.from({ length: 30 }, (_, i) => message(i + 1))));
  list.scrollTop = 305;
  list.fire('scroll');
  const anchor = list.children[10]!;
  const oldTop = anchor.getBoundingClientRect().top;
  list.clientWidth = 240;
  list.wrappedHeight = 30;
  list.fire('scroll'); // Browser layout/clamping can queue this before ResizeObserver.
  resize();
  assert.equal(anchor.getBoundingClientRect().top, oldTop);
  // Subsequent user scrolling, with unchanged geometry, replaces the anchor.
  list.scrollTop = 725;
  list.fire('scroll');
  const newAnchor = list.children[12]!;
  const newTop = newAnchor.getBoundingClientRect().top;
  list.clientWidth = 200;
  list.wrappedHeight = 60;
  resize();
  assert.equal(newAnchor.getBoundingClientRect().top, newTop);
});

test('readability and latest-jump changes refresh the resize anchor without rebuilding chat', () => {
  const { document, list, larger, jump, resize } = fixture();
  renderChat(document, frame(30, Array.from({ length: 30 }, (_, i) => message(i + 1))));
  list.scrollTop = 305;
  list.fire('scroll');
  const anchor = list.children[10]!;
  const oldTop = anchor.getBoundingClientRect().top;
  larger.fire('click');
  list.clientWidth = 240;
  list.wrappedHeight = 30;
  resize();
  assert.equal(anchor.getBoundingClientRect().top, oldTop);
  jump.fire('click');
  list.scrollTop = list.scrollHeight - list.clientHeight;
  list.clientHeight = 70;
  resize();
  assert.equal(list.scrollTop, list.scrollHeight);
  assert.equal(jump.hidden, true);
  assert.equal(list.replaced, 1);
});

test('resize after revoke cannot restore old private rows and a new empty subscription stays empty', () => {
  const { document, list, jump, resize } = fixture();
  renderChat(document, frame(30, Array.from({ length: 30 }, (_, i) => message(i + 1))));
  list.scrollTop = 305;
  list.fire('scroll');
  const old = list.children[10]!;
  renderChat(document, frame(30, [], 'revoked'));
  list.clientWidth = 240;
  resize();
  assert.equal(list.children.length, 0);
  assert.equal(old.parent, null);
  assert.equal(jump.hidden, true);
  renderChat(document, frame(0, []));
  list.clientHeight = 80;
  resize();
  assert.equal(list.children.length, 0);
  renderChat(document, frame(1, [message(31)]));
  assert.equal(list.children.length, 1);
  assert.notEqual(list.children[0], old);
  assert.equal(jump.hidden, true);
});
