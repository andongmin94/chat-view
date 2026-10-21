// SPDX-License-Identifier: GPL-2.0-or-later
// Page projection only; the separate flow test exercises real HTTP/SQLite/ws.
import test from 'node:test';
import assert from 'node:assert/strict';
import { campaignsPage } from '../server/ads/pages.mts';

const cases = [
  ['provider-required', false, false, false, 'provider-required'],
  ['streaming-required', false, false, false, 'streaming-required'],
  ['ready', false, false, false, 'not-selected'],
  ['ready', false, true, true, 'not-selected'],
  ['ready', true, false, false, 'streaming-disconnected'],
  ['ready', true, true, false, 'waiting-report'],
  ['ready', true, true, true, 'report-ready'],
  // Stale/inconsistent view inputs must not override prerequisite failures.
  ['provider-required', true, true, true, 'provider-required'],
  ['streaming-required', true, true, true, 'streaming-required'],
  ['ready', true, false, true, 'streaming-disconnected'],
] as const;
for (const [selectionState, selected, streamingConnected, previewReady, expected] of cases) {
  test(`recovery ${selectionState}/${selected}/${streamingConnected}/${previewReady} -> ${expected}`, () => {
    const url = `https://service.invalid/public/ads/${'1'.repeat(32)}`;
    const html = campaignsPage({ selected, sourceId: '1'.repeat(32) }, 'a'.repeat(64),
      'https://service.invalid', { selectionState, streamingConnected, previewReady,
        output: { state: 'unknown' }, viewToken: 'b'.repeat(64) });
    const recovery = /<div data-banner-recovery-state="([^"]+)">([\s\S]*?)<\/div>/u.exec(html);
    assert(recovery);
    assert.equal(recovery[1], expected);
    assert.equal((html.match(/data-banner-recovery-state=/gu) ?? []).length, 1);
    assert.equal(html.includes('data-public-banner-state="report-ready"'), expected === 'report-ready');
    assert(html.includes(`value="${url}"`), 'recovery keeps the exact public URL');
    assert.match(html, /href="\/campaigns">현재 상태 새로고침/u);
    assert.match(html, /href="\/account#two-pc-status"/u);
    assert.match(html, /id="public-banner-status"/u);
    assert.doesNotMatch(recovery[2]!, /<form|<script|<button/u, 'guidance adds no privileged action');
    for (const link of recovery[2]!.matchAll(/href="#([^"]+)"/gu))
      assert(html.includes(`id="${link[1]}"`), 'in-page next step exists');
    if (expected === 'streaming-disconnected') assert.match(recovery[2]!, /현재 승인으로 자체 채팅 복귀/u);
    if (expected === 'streaming-disconnected' || expected === 'waiting-report')
      assert.match(recovery[2]!, /캠페인 재선택이나 URL 교체는 필요하지 않습니다/u);
    if (expected === 'waiting-report') assert.match(recovery[2]!, /방송·녹화를 시작할 필요는 없습니다/u);
    if (expected === 'report-ready') assert.match(recovery[2]!, /시청자 노출은 아직 확인된 것이 아닙니다/u);
    assert.doesNotMatch(html, /같은 URL을 다시 사용하려면 캠페인을 명시적으로 재선택하세요/u,
      'report expiry is no longer confused with revoked selection');
  });
}

test('creator sees real output-report distinctions separately from public test-banner readiness', () => {
  const states = [
    { output: { state: 'unknown' } as const, expected: 'unknown', pattern: /미확인/u },
    { output: { state: 'reported', streaming: false, recording: false, expiresInMs: 8000 } as const,
      expected: 'stopped', pattern: /스트리밍·녹화 모두 비활성/u },
    { output: { state: 'reported', streaming: true, recording: false, expiresInMs: 8000 } as const,
      expected: 'streaming', pattern: /스트리밍 활성·녹화 비활성/u },
    { output: { state: 'reported', streaming: false, recording: true, expiresInMs: 8000 } as const,
      expected: 'recording', pattern: /스트리밍 비활성·녹화 활성/u },
    { output: { state: 'reported', streaming: true, recording: true, expiresInMs: 8000 } as const,
      expected: 'streaming-recording', pattern: /스트리밍·녹화 모두 활성/u },
  ];
  for (const { output, expected, pattern } of states) {
    const ready = output.state === 'reported';
    const html = campaignsPage({ selected: true, sourceId: '1'.repeat(32) }, 'a'.repeat(64),
      'https://service.invalid', { selectionState: 'ready', streamingConnected: true, previewReady: ready,
        output, viewToken: 'b'.repeat(64) });
    const paragraph = /<p data-output-report-state="([^"]+)">([^<]+)<\/p>/u.exec(html);
    assert(paragraph);
    assert.equal(paragraph[1], expected);
    assert.match(paragraph[2]!, pattern);
    assert.equal((html.match(/data-output-report-state=/gu) ?? []).length, 1);
    assert.equal(html.includes('data-public-banner-state="report-ready"'), ready,
      'stopped and recording-only flags can permit preview but never imply a live broadcast');
    assert.match(html, /최대 15초 유효/u);
    assert.match(html, /자동 갱신되지 않습니다/u);
    assert.match(html, /광고.*시청자 광고 노출/u);
    assert.doesNotMatch(paragraph[2]!, /확정 수익|실측 광고 시청|HP 감소/u);
  }
});
