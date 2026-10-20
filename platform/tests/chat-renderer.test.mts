// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { renderChat } from '../web/chat-renderer.js';

class Element {
  readonly tag: string;
  children: Element[] = [];
  textContent = '';
  className = '';
  dir = '';
  scrollTop = 0;
  clientHeight = 200;
  replaced = 0;
  added = 0;
  removed = 0;
  hidden = true;
  private readonly listeners = new Map<string, () => void>();
  constructor(tag: string) { this.tag = tag; }
  addEventListener(name: string, listener: () => void) { this.listeners.set(name, listener); }
  fire(name: string) { this.listeners.get(name)?.(); }
  set innerHTML(_value: string) { throw new Error('HTML injection'); }
  get scrollHeight() { return this.children.length * 30; }
  get firstChild() { return this.children[0]; }
  append(...children: Element[]) { this.children.push(...children); }
  appendChild(child: Element) { this.added++; this.children.push(child); }
  removeChild(child: Element) {
    assert.equal(this.firstChild, child);
    this.removed++; this.children.shift();
  }
  replaceChildren(...children: Element[]) { this.replaced++; this.children = children; }
}
function fixture() {
  const list = new Element('ol'), status = new Element('p'), jump = new Element('button');
  const document = { getElementById: (id: string) =>
    id === 'status' ? status : id === 'messages' ? list : id === 'jump-latest' ? jump : null,
    createElement: (tag: string) => new Element(tag) };
  return { list, status, jump, document };
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
