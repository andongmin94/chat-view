// SPDX-License-Identifier: GPL-2.0-or-later
// Shared by the browser preview and the embedded native HUD. No transport here.
export function renderChat(document, snapshot) {
  const status = document.getElementById('status');
  const list = document.getElementById('messages');
  const labels = {
    idle: '연결 관리에서 채팅 수신을 시작하세요.', connecting: '치지직 소켓 연결 중',
    subscribing: '채팅 구독 확인 중', disconnected: '연결이 끊겼습니다. 연결 관리에서 다시 시작하세요.',
    revoked: '채팅 권한이 철회됐습니다. 다시 로그인하세요.',
    unsubscribed: '채팅 구독이 취소됐습니다.', error: '채팅 연결 실패. 연결 관리에서 다시 시도하세요.',
    stopped: '채팅 수신이 중지됐습니다.',
  };
  status.textContent = snapshot.state === 'subscribed'
    ? (snapshot.received ? `메시지 ${snapshot.received}개 수신 · 최근 100개만 표시` : '구독 확인 완료 · 첫 메시지 대기 중')
    : (labels[snapshot.state] ?? '연결 상태를 확인할 수 없습니다.');
  const nearBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 80;
  const rows = [];
  if (snapshot.state === 'subscribed') {
    for (const message of snapshot.messages.slice(-100)) {
      const row = document.createElement('li');
      const name = document.createElement('b'); name.textContent = message.nickname;
      name.dir = 'auto';
      const time = document.createElement('time');
      time.textContent = new Date(message.messageTime).toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit' });
      const content = document.createElement('span'); content.className = 'content';
      content.dir = 'auto'; content.textContent = message.content;
      row.append(time, name, content); rows.push(row);
    }
  }
  list.replaceChildren(...rows);
  if (nearBottom) list.scrollTop = list.scrollHeight;
}
