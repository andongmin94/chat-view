// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { nativeChatSnapshot, connectNativeChat } from '../web/native-chat.js';
import { renderChat } from '../web/chat-renderer.js';

const message = { nickname: '한글 <script>', content: '<img src=x onerror=evil()> 😀', messageTime: 1700000000000 };
const snapshot = { state: 'subscribed', received: 1, messages: [message] };
const envelope = { type: 'chat-snapshot', version: 1, frameId: '1', snapshot };
class Element {
  textContent = ''; dir = ''; className = ''; children: Element[] = [];
  scrollHeight = 0; scrollTop = 0; clientHeight = 0;
  hidden = true;
  private readonly listeners = new Map<string, () => void>();
  addEventListener(name: string, fn: () => void) { this.listeners.set(name, fn); }
  fire(name: string) { this.listeners.get(name)?.(); }
  set innerHTML(_value: string) { throw new Error('HTML injection'); }
  append(...children: Element[]) { this.children.push(...children); }
  replaceChildren(...children: Element[]) { this.children = children; }
}
function fixture() {
  const status = new Element(); const list = new Element(); const jump = new Element();
  const document = { getElementById: (id: string) =>
    id === 'status' ? status : id === 'messages' ? list : id === 'jump-latest' ? jump : null,
    createElement: () => new Element() };
  let listener: (event: any) => void = () => {};
  let watchdog = () => {}; let now = 0; const sent: string[] = [];
  const bridge = {
    addEventListener(_name: string, callback: typeof listener) { listener = callback; },
    removeEventListener() { listener = () => {}; }, postMessage(value: string) { sent.push(value); },
  };
  const clock = { performance: { now: () => now }, setInterval(callback: () => void) { watchdog = callback; return 1; }, clearInterval() { watchdog = () => {}; } };
  const close = connectNativeChat(document, bridge, renderChat, 'test-nonce', clock as any);
  return { status, list, jump, sent, close, emit(data: unknown, trusted = true, source: unknown = bridge) { listener({ data, isTrusted: trusted, source }); }, tick(ms: number) { now += ms; watchdog(); } };
}
test('native bridge displays text through the same renderer and sends counts, never private content', () => {
  const f = fixture(); assert.deepEqual(f.sent, ['chat-ready:test-nonce']); f.emit(envelope);
  assert.equal(f.list.children[0]?.children[1]?.textContent, message.nickname);
  assert.equal(f.list.children[0]?.children[2]?.textContent, message.content);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:1:1');
  assert.doesNotMatch(f.sent.join(''), /PRIVATE|onerror|한글/u); f.close();
});
test('native delivery requires the bound bridge, not the undocumented DOM trust flag', () => {
  const f = fixture(); f.emit(envelope, false, {}); f.emit(envelope, true, {});
  assert.equal(f.list.children.length, 0); assert.equal(f.sent.length, 1);
  f.emit(envelope, false);
  assert.equal(f.list.children.length, 1);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:1:1'); f.close();
});

test('per-delivery ACK carries only the exact native frame ID; malformed IDs cannot authorize a render', () => {
  const f = fixture();
  f.emit(envelope);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:1:1');
  f.emit({ ...envelope, frameId: '2', snapshot: {
    state: 'subscribed', received: 1,
    messages: [{ ...message, content: 'second frame' }],
  } });
  assert.equal(f.list.children[0]?.children[2]?.textContent, 'second frame');
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:2:1');
  for (const frameId of ['', '0', '01', '-1', '4294967296', 3, null, '3:1']) {
    f.emit({ ...envelope, frameId });
    assert.equal(f.list.children.length, 0, 'bad delivery IDs fail closed');
    assert.equal(f.sent.at(-1), 'chat-invalid:test-nonce:');
  }
  assert.doesNotMatch(f.sent.join(''), /onerror|한글|second frame/u);
  f.close();
});

test('native WebView2 bridge acknowledges each heartbeat without rebuilding an unchanged text row', () => {
  const f = fixture();
  f.emit(envelope);
  const row = f.list.children[0];
  f.emit({ ...envelope, frameId: '2', snapshot: { state: 'subscribed', received: 1,
    messages: [{ ...message }] } });
  assert.equal(f.list.children[0], row);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:2:1');
  assert.doesNotMatch(f.sent.join(''), /PRIVATE|onerror|한글/u);
  f.close();
});

test('prewarmed own document stays idle during browser consent; liveness begins only after first frame', () => {
  const f = fixture();
  f.tick(60_000); // The real browser confirmation can stay pending for minutes.
  assert.equal(f.list.children.length, 0);
  assert.match(f.status.textContent, /연결 관리/u);
  assert.deepEqual(f.sent, ['chat-ready:test-nonce'], 'pre-consent document cannot invent chat frames');
  f.emit(envelope);
  assert.equal(f.list.children.length, 1);
  f.tick(14_999);
  assert.equal(f.list.children.length, 1, 'live chat retains its original 15 second budget');
  f.tick(2);
  assert.equal(f.list.children.length, 0);
  assert.match(f.status.textContent, /끊겼/u);
  f.emit({ ...envelope, frameId: '2' });
  assert.equal(f.list.children.length, 1, 'a fresh valid frame restores the same owned DOM');
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:2:1');
  f.close();
});

test('host-state does not keep a dead chat feed alive', () => {
  const f = fixture(); f.emit(envelope); f.tick(14000);
  f.emit({ type: 'host-state', editing: true }); f.tick(1001);
  assert.equal(f.list.children.length, 0); assert.match(f.status.textContent, /끊겼/u); f.close();
});
test('new feed updates restore display; stop/revoke clear even an attached stale history', () => {
  const f = fixture(); f.emit(envelope); f.tick(16000); f.emit(envelope);
  assert.equal(f.list.children.length, 1);
  f.emit({ ...envelope, snapshot: { state: 'revoked', received: 1, messages: [message] } });
  assert.equal(f.list.children.length, 0); f.close();
});
test('closing detaches message delivery and timer', () => {
  const f = fixture(); f.emit(envelope); f.close(); f.close(); f.emit(envelope); f.tick(60000);
  assert.equal(f.list.children.length, 0);
});
test('unknown protocol and malformed display data clear old content without throwing', () => {
  const f = fixture(); f.emit(envelope); f.emit({ ...envelope, version: 2 });
  assert.equal(f.list.children.length, 0); assert.equal(f.sent.at(-1), 'chat-invalid:test-nonce:1');
  f.emit(envelope); f.emit({ ...envelope, snapshot: null }); assert.equal(f.list.children.length, 0); f.close();
});
for (const invalid of [null, [], {}, { ...snapshot, state: 'paid' }, { ...snapshot, received: NaN }, { ...snapshot, received: -1 }, { ...snapshot, received: 0 }, { ...snapshot, messages: Array(101).fill(message), received: 101 }, { ...snapshot, messages: [{ ...message, nickname: 'x'.repeat(201) }] }, { ...snapshot, messages: [{ ...message, content: 'x'.repeat(10001) }] }, { ...snapshot, messages: [{ ...message, content: '\u0000' }] }, { ...snapshot, messages: [{ ...message, messageTime: 8640000000000001 }] }]) {
  test('invalid native display snapshot is rejected', () => assert.throws(() => nativeChatSnapshot(invalid)));
}
test('display projection drops credentials and unknown fields; identical messages are preserved', () => {
  const result = nativeChatSnapshot({ ...snapshot, received: 2, accessToken: 'PRIVATE', messages: [{ ...message, token: 'PRIVATE' }, message] });
  assert.deepEqual(result.messages, [message, message]); assert.doesNotMatch(JSON.stringify(result), /PRIVATE/u);
});
test('all lifecycle states clear content except subscribed', () => {
  for (const state of ['idle', 'connecting', 'subscribing', 'disconnected', 'revoked', 'unsubscribed', 'error', 'stopped']) {
    assert.deepEqual(nativeChatSnapshot({ state, received: 1, messages: [message] }).messages, []);
  }
});
