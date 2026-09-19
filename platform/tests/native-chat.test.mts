// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { nativeChatSnapshot, connectNativeChat } from '../web/native-chat.js';
import { renderChat } from '../web/chat-renderer.js';

const message = { nickname: '한글 <script>', content: '<img src=x onerror=evil()> 😀', messageTime: 1700000000000 };
const snapshot = { state: 'subscribed', received: 1, messages: [message] };
const envelope = { type: 'chat-snapshot', version: 1, snapshot };
class Element {
  textContent = ''; dir = ''; className = ''; children: Element[] = [];
  scrollHeight = 0; scrollTop = 0; clientHeight = 0;
  set innerHTML(_value: string) { throw new Error('HTML injection'); }
  append(...children: Element[]) { this.children.push(...children); }
  replaceChildren(...children: Element[]) { this.children = children; }
}
function fixture() {
  const status = new Element(); const list = new Element();
  const document = { getElementById: (id: string) => id === 'status' ? status : list, createElement: () => new Element() };
  let listener: (event: any) => void = () => {};
  let watchdog = () => {}; let now = 0; const sent: string[] = [];
  const bridge = {
    addEventListener(_name: string, callback: typeof listener) { listener = callback; },
    removeEventListener() { listener = () => {}; }, postMessage(value: string) { sent.push(value); },
  };
  const clock = { performance: { now: () => now }, setInterval(callback: () => void) { watchdog = callback; return 1; }, clearInterval() { watchdog = () => {}; } };
  const close = connectNativeChat(document, bridge, renderChat, 'test-nonce', clock as any);
  return { status, list, sent, close, emit(data: unknown, trusted = true, source: unknown = bridge) { listener({ data, isTrusted: trusted, source }); }, tick(ms: number) { now += ms; watchdog(); } };
}
test('native bridge displays text through the same renderer and sends counts, never private content', () => {
  const f = fixture(); assert.deepEqual(f.sent, ['chat-ready:test-nonce']); f.emit(envelope);
  assert.equal(f.list.children[0]?.children[1]?.textContent, message.nickname);
  assert.equal(f.list.children[0]?.children[2]?.textContent, message.content);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:1');
  assert.doesNotMatch(f.sent.join(''), /PRIVATE|onerror|한글/u); f.close();
});
test('native delivery requires the bound bridge, not the undocumented DOM trust flag', () => {
  const f = fixture(); f.emit(envelope, false, {}); f.emit(envelope, true, {});
  assert.equal(f.list.children.length, 0); assert.equal(f.sent.length, 1);
  f.emit(envelope, false);
  assert.equal(f.list.children.length, 1);
  assert.equal(f.sent.at(-1), 'chat-rendered:test-nonce:1'); f.close();
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
  assert.equal(f.list.children.length, 0); assert.equal(f.sent.at(-1), 'chat-invalid:test-nonce');
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
