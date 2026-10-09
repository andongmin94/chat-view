// SPDX-License-Identifier: GPL-2.0-or-later
// The management page copies only the already-public OBS test source URL.
// It makes no requests, stores no state and never claims an OBS/viewer receipt.
(() => {
  const source = document.getElementById('source-url');
  const button = document.getElementById('copy-public-source');
  const result = document.getElementById('copy-public-result');
  if (!source || !button || !result) return;

  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(source.value);
      result.textContent = '공개 배너 주소를 복사했습니다. OBS의 URL 칸에 붙여넣으세요.';
    } catch {
      // Plain HTTP other than loopback or browser clipboard policy may
      // deny writes. Allow normal keyboard copying without weakening CSP.
      source.focus();
      source.select();
      result.textContent = '자동 복사가 차단됐습니다. 선택된 주소를 Ctrl+C(맥은 ⌘C)로 복사하세요.';
    }
  });
})();
