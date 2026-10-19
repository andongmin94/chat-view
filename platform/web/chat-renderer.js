// SPDX-License-Identifier: GPL-2.0-or-later
// Shared by the browser preview and the embedded native HUD. No transport here.
// A heartbeat repeats chat already shown: preserve DOM rows and scroll position.
const lastRendered = new WeakMap();
const sameMessage = (a, b) => a.nickname === b.nickname &&
  a.content === b.content && a.messageTime === b.messageTime;

function rowFor(document, message) {
  const row = document.createElement('li');
  const name = document.createElement('b'); name.textContent = message.nickname;
  name.dir = 'auto';
  const time = document.createElement('time');
  time.textContent = new Date(message.messageTime).toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit' });
  const content = document.createElement('span'); content.className = 'content';
  content.dir = 'auto'; content.textContent = message.content;
  row.append(time, name, content);
  return row;
}

export function renderChat(document, snapshot) {
  const status = document.getElementById('status');
  const list = document.getElementById('messages');
  const labels = {
    idle: '연결 관리에서 채팅 수신을 시작하세요.', connecting: '치지직 소켓 연결 중',
    subscribing: '채팅 구독 확인 중', disconnected: '치지직 채팅 연결이 끊겼습니다. 복구되지 않으면 연결 관리에서 다시 시도하세요.',
    revoked: '채팅 권한이 철회됐습니다. 다시 로그인하세요.',
    unsubscribed: '채팅 구독이 취소됐습니다.', error: '채팅 연결 실패. 연결 관리에서 다시 시도하세요.',
    stopped: '채팅 수신이 중지됐습니다.',
  };
  const label = snapshot.state === 'subscribed'
    ? (snapshot.received ? `메시지 ${snapshot.received}개 수신 · 최근 100개만 표시` : '구독 확인 완료 · 첫 메시지 대기 중')
    : (labels[snapshot.state] ?? '연결 상태를 확인할 수 없습니다.');
  if (status.textContent !== label) status.textContent = label;

  const messages = snapshot.state === 'subscribed'
    ? snapshot.messages.slice(-100).map(message => ({
        nickname: message.nickname, content: message.content, messageTime: message.messageTime,
      }))
    : [];
  const previous = lastRendered.get(list);
  if (previous && previous.state === snapshot.state &&
      previous.received === snapshot.received &&
      previous.messages.length === messages.length &&
      messages.every((message, i) => sameMessage(message, previous.messages[i]))) return;

  const nearBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 80;
  // CHZZK appends messages and retains up to 100. Check their content before
  // reusing nodes: duplicate text does not identify a delivery or a session.
  let kept = 0;
  if (previous?.state === 'subscribed' && snapshot.state === 'subscribed' &&
      list.children.length === previous.messages.length && snapshot.received > previous.received) {
    const delivered = snapshot.received - previous.received;
    const overlap = Math.min(previous.messages.length, messages.length - delivered);
    if (overlap > 0 && messages.slice(0, overlap).every((message, i) =>
        sameMessage(message, previous.messages[previous.messages.length - overlap + i]))) {
      kept = overlap;
    }
  }
  if (kept) {
    for (let i = 0; i < previous.messages.length - kept; i++) list.removeChild(list.firstChild);
    for (const message of messages.slice(kept)) list.appendChild(rowFor(document, message));
  } else {
    list.replaceChildren(...messages.map(message => rowFor(document, message)));
  }
  if (nearBottom) list.scrollTop = list.scrollHeight;
  lastRendered.set(list, { state: snapshot.state, received: snapshot.received, messages });
}
