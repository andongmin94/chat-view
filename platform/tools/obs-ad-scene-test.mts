// SPDX-License-Identifier: GPL-2.0-or-later
// Dedicated streaming-PC flow check: official OBS + its real CEF Browser Source.
// Provider/account/output reports are synthetic. No stream, recording, gaming-PC
// OBS, viewer attention, HUD exclusion or payable accounting is claimed here.
import assert from 'node:assert/strict';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { spawn, execFile } from 'node:child_process';
import { once } from 'node:events';
import { createServer } from 'node:net';
import { mkdir, writeFile, readFile, access, rm } from 'node:fs/promises';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { setTimeout as delay } from 'node:timers/promises';
import { WebSocket } from 'ws';
import { fixture } from '../tests/fixtures/service.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import { TEST_CAMPAIGN } from '../server/ads/campaigns.mts';

const exec = promisify(execFile);
assert.equal(process.platform, 'win32', 'official Windows OBS fixture required');
assert(process.argv[2] && process.argv[3], 'isolated OBS root and evidence directory required');
const root = resolve(process.argv[2]!), evidence = resolve(process.argv[3]!);
assert.equal((await readFile(join(root, '.chatview-ad-fixture'), 'utf8')).trim(), 'synthetic-only');
const config = join(root, 'config', 'obs-studio');
try { await access(config); assert.fail('refuse an existing OBS profile'); }
catch (error) { if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error; }
const executable = join(root, 'bin', '64bit', 'obs64.exe'); await access(executable);
await mkdir(evidence, { recursive: true });
const websocketConfig = join(config, 'plugin_config', 'obs-websocket', 'config.json');
await mkdir(dirname(websocketConfig), { recursive: true, mode: 0o700 });
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const address = reservation.address(); assert(address && typeof address !== 'string');
const port = address.port;
await new Promise<void>(yes => reservation.close(() => yes()));
const password = randomBytes(32).toString('hex');
await writeFile(websocketConfig, JSON.stringify({ first_load: false, server_enabled: true,
  server_port: port, alerts_enabled: false, auth_required: true, server_password: password }), { mode: 0o600 });

// Test-only bounded request/response transport using the existing ws dependency.
// This is not a new product OBS connection, SDK or unauthenticated RPC server.
// Protocol: obsproject/obs-websocket/docs/generated/protocol.md (RPC v1).
async function connectControl(): Promise<{ call: (name: string, data?: Record<string, unknown>) => Promise<any>; close: () => void }> {
  const deadline = performance.now() + 30000;
  while (performance.now() < deadline) {
    assert(!spawnFailed && child.exitCode === null, 'OBS exited before control readiness');
    const socket = new WebSocket(`ws://127.0.0.1:${port}`, { handshakeTimeout: 1500, maxPayload: 2 * 1024 * 1024 });
    let ready = false;
    type Pending = { resolve: (data: unknown) => void; reject: (error: Error) => void };
    const pending = new Map<string, Pending>();
    let accepted!: () => void, rejected!: (e: Error) => void;
    const identified = new Promise<void>((yes, no) => { accepted = yes; rejected = no; });
    const fail = () => { const error = new Error('OBS test control unavailable'); rejected(error);
      for (const item of pending.values()) item.reject(error); pending.clear(); };
    socket.on('error', fail); socket.on('close', fail);
    socket.on('message', (bytes, binary) => {
      try {
        assert.equal(binary, false);
        const value = JSON.parse(String(bytes));
        if (value.op === 0) {
          assert(!ready && value.d.rpcVersion >= 1 && value.d.authentication);
          const { salt, challenge } = value.d.authentication;
          assert(typeof salt === 'string' && typeof challenge === 'string' && salt.length < 256 && challenge.length < 256);
          const hash = (text: string) => createHash('sha256').update(text).digest('base64');
          socket.send(JSON.stringify({ op: 1, d: { rpcVersion: 1,
            authentication: hash(hash(password + salt) + challenge), eventSubscriptions: 0 } }));
        } else if (value.op === 2) { assert.equal(value.d.negotiatedRpcVersion, 1); ready = true; accepted(); }
        else if (value.op === 7) {
          const item = pending.get(value.d.requestId); if (!item) return;
          pending.delete(value.d.requestId);
          if (value.d.requestStatus.result === true) item.resolve(value.d.responseData ?? {});
          else item.reject(new Error(`OBS request rejected (${value.d.requestStatus.code})`));
        }
      } catch { fail(); socket.terminate(); }
    });
    const timer = setTimeout(() => { fail(); socket.terminate(); }, 2500);
    try { await identified; }
    catch { socket.terminate(); clearTimeout(timer); await delay(250); continue; }
    clearTimeout(timer);
    return { close: () => socket.terminate(), async call(name, data = {}) {
      assert.equal(pending.size, 0, 'test RPCs must be serial');
      const id = randomUUID(); let timeout: ReturnType<typeof setTimeout> | undefined;
      try {
        return await new Promise((yes, no) => {
          pending.set(id, { resolve: yes, reject: no });
          timeout = setTimeout(() => { no(new Error(`OBS ${name} timed out`)); socket.terminate(); }, 8000);
          socket.send(JSON.stringify({ op: 6, d: { requestType: name, requestId: id, requestData: data } }));
        });
      } finally { clearTimeout(timeout); pending.delete(id); }
    } };
  }
  throw new Error('OBS authenticated control did not become ready');
}

const f = await fixture({ createDisplay: (approval, origin, snapshot) => new DisplayGateway(approval, origin, snapshot) });
const child = spawn(executable, ['--portable', '--multi', '--disable-updater', '--disable-missing-files-check',
  '--disable-shutdown-check', '--websocket_ipv4_only'], { cwd: dirname(executable), stdio: 'ignore' });
let spawnFailed = false;
child.on('error', () => { spawnFailed = true; });
const exited = once(child, 'exit');
void exited.catch(() => {});
let control: Awaited<ReturnType<typeof connectControl>> | undefined;
let heartbeat: ReturnType<typeof setInterval> | undefined, reporting = false, reportError = false;
let reads = 0, documents = 0, stalled = false, sawObs = false;
const original = f.app.handle.bind(f.app);
f.app.handle = async (request, response) => {
  if (request.url?.startsWith('/public/ads/')) {
    assert.equal(request.headers.authorization, undefined); assert.equal(request.headers.cookie, undefined);
    sawObs ||= /OBS\//u.test(request.headers['user-agent'] ?? '');
    if (/\/public\/ads\/[a-f0-9]{32}$/u.test(request.url)) documents++;
    if (request.url.endsWith('/state')) { reads++; if (stalled) return; }
  }
  await original(request, response);
};
const captures: Record<string, unknown>[] = [];
const pixelScript = fileURLToPath(new URL('../../tests/check-ad-pixels.ps1', import.meta.url));
const main = 'ChatView Ad Fixture', away = 'ChatView Empty Fixture', input = 'ChatView Public Test Ad';
try {
  control = await connectControl(); const call = control.call;
  const version = await call('GetVersion'); assert.equal(version.obsVersion, process.env.OBS_VERSION);
  assert(version.availableRequests.includes('SaveSourceScreenshot'));
  await call('SetStudioModeEnabled', { studioModeEnabled: false });
  await call('SetVideoSettings', { baseWidth: 1280, baseHeight: 720, outputWidth: 1280, outputHeight: 720,
    fpsNumerator: 30, fpsDenominator: 1 });
  for (const sceneName of [main, away]) {
    await call('CreateScene', { sceneName });
    await call('CreateInput', { sceneName, inputName: `${sceneName} background`, inputKind: 'color_source_v3',
      inputSettings: { color: 0xff5aaa2f, width: 1280, height: 720 }, sceneItemEnabled: true });
  }
  await call('SetCurrentProgramScene', { sceneName: main });
  const alice = await f.connect('obs-ad-fixture', 'streaming');
  await f.page('/campaigns', alice.b);
  const select = async () => assert.equal((await f.post(`/campaigns/${TEST_CAMPAIGN.id}/select`, alice.b)).status, 303);
  await select();
  const page = await f.page('/campaigns', alice.b);
  const id = /\/public\/ads\/([a-f0-9]{32})/u.exec(page)?.[1]; assert(id);
  const peer = new WebSocket(f.origin.replace('http:', 'ws:') + '/display/events', {
    headers: { Authorization: `Bearer ${alice.lease.token}` }, handshakeTimeout: 3000 });
  peer.on('error', () => {}); await once(peer, 'open');
  let sequence = 0;
  const report = async () => {
    if (reporting) return;
    reporting = true;
    try {
      const response = await f.request('/broadcast/output', { method: 'POST', headers: {
        Authorization: `ChatView-Output ${alice.lease.outputToken}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ sequence: ++sequence, streaming: false, recording: false, sampleAgeMs: 0 }) });
      assert.equal(response.status, 200);
    } finally { reporting = false; }
  };
  await report(); heartbeat = setInterval(() => { void report().catch(() => { reportError = true; }); }, 2000);
  const { sceneItemId } = await call('CreateInput', { sceneName: main, inputName: input, inputKind: 'browser_source',
    inputSettings: { url: `${f.origin}/public/ads/${id}`, width: 960, height: 180, webpage_control_level: 0,
      shutdown: false, restart_when_active: false, fps_custom: true, fps: 30, reroute_audio: false }, sceneItemEnabled: true });
  const setPlacement = async (x: number, y: number, scale: number) => {
    await call('SetSceneItemTransform', { sceneName: main, sceneItemId, sceneItemTransform: {
      positionX: x, positionY: y, scaleX: scale, scaleY: scale, alignment: 5, rotation: 0, boundsType: 'OBS_BOUNDS_NONE' } });
    const { sceneItemTransform: t } = await call('GetSceneItemTransform', { sceneName: main, sceneItemId });
    assert.equal(t.positionX, x); assert.equal(t.positionY, y);
    assert.equal(t.sourceWidth, 960); assert.equal(t.sourceHeight, 180);
    assert.equal(t.scaleX, scale); assert.equal(t.scaleY, scale);
  };
  const capture = async (label: string, sceneName: string, rectangle = [0, 0, 0, 0], timeout = 15000) => {
    const path = join(evidence, `${label}.png`), deadline = performance.now() + timeout;
    let metrics: Record<string, unknown> = {};
    do {
      await call('SaveSourceScreenshot', { sourceName: sceneName, imageFormat: 'png', imageFilePath: path,
        imageWidth: 1280, imageHeight: 720 });
      const result = await exec('powershell.exe', ['-NoProfile', '-NonInteractive', '-File', pixelScript, '-Image', path,
        '-X', String(rectangle[0]), '-Y', String(rectangle[1]), '-Width', String(rectangle[2]), '-Height', String(rectangle[3])],
      { timeout: 8000, maxBuffer: 4096 });
      metrics = JSON.parse(result.stdout.trim());
      if (metrics.matches === true) {
        captures.push({ label, rectangle, ...metrics }); console.log(`${label}: composite pixels passed`); return;
      }
      await delay(250);
    } while (performance.now() < deadline);
    throw new Error(`${label}: composite pixels did not match ${JSON.stringify(metrics)}`);
  };
  const enabled = (value: boolean) => call('SetSceneItemEnabled', { sceneName: main, sceneItemId, sceneItemEnabled: value });
  const stableHiddenReads = async () => {
    await delay(500); const began = reads; await delay(2500);
    assert.equal(reads, began, 'hidden retained OBS source must not keep polling');
  };
  await setPlacement(160, 420, 1); await capture('placed-960', main, [160, 420, 960, 180]);
  const loadedDocuments = documents;
  await setPlacement(60, 50, 0.5); await capture('scaled-480', main, [60, 50, 480, 90]);
  await enabled(false); await stableHiddenReads(); await capture('local-hide', main);
  assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
  const beforeReturn = reads;
  await enabled(true);
  const freshDeadline = performance.now() + 3000;
  while (reads <= beforeReturn && performance.now() < freshDeadline) await delay(50);
  assert(reads > beforeReturn, 'show must start a fresh read');
  await capture('return-after-stop', main);
  await select(); await capture('reselected', main, [60, 50, 480, 90]);
  await call('SetCurrentProgramScene', { sceneName: away });
  await stableHiddenReads(); await capture('scene-away', away);
  stalled = true;
  await call('SetCurrentProgramScene', { sceneName: main });
  await delay(750); await capture('return-during-outage', main);
  assert.equal(documents, loadedDocuments, 'hide/show must work without reloading the browser document');
  stalled = false; await capture('scene-return', main, [60, 50, 480, 90]);
  await call('SetStudioModeEnabled', { studioModeEnabled: true });
  await call('SetCurrentProgramScene', { sceneName: away });
  await call('SetCurrentPreviewScene', { sceneName: main });
  await delay(750); await capture('inactive-preview', main, [60, 50, 480, 90]);
  // Both documented OBS lifecycle settings: retained page above, unload on hide below.
  await call('SetStudioModeEnabled', { studioModeEnabled: false });
  await call('SetCurrentProgramScene', { sceneName: main });
  await call('SetInputSettings', { inputName: input, inputSettings: { shutdown: true, restart_when_active: true }, overlay: true });
  await capture('shutdown-mode-visible', main, [60, 50, 480, 90]);
  await enabled(false); await stableHiddenReads(); await capture('shutdown-mode-hidden', main);
  await enabled(true); await capture('shutdown-mode-return', main, [60, 50, 480, 90]);
  assert.equal((await f.post('/campaigns/stop', alice.b)).status, 303);
  await capture('campaign-stop', main);
  const nextFrame = once(peer, 'message'); f.chats.get('obs-ad-fixture')!.publish('private channel stays separate');
  await nextFrame; assert.equal(peer.readyState, WebSocket.OPEN);
  assert(sawObs, 'requests must come from official OBS CEF, not WebView2');
  assert.equal(reportError, false);
  const settings = await call('GetInputSettings', { inputName: input });
  assert.equal(settings.inputSettings.webpage_control_level, 0);
  assert.equal((await call('GetStreamStatus')).outputActive, false);
  assert.equal((await call('GetRecordStatus')).outputActive, false);
  await writeFile(join(evidence, 'result.json'), JSON.stringify({ obsVersion: version.obsVersion,
    scope: 'Actual OBS CEF and composed scene pixels; synthetic provider/output; no broadcast or payout',
    captures, publicReads: reads, documents }, null, 2));
  console.log(`Official OBS public banner flow passed (${captures.length} composite checks)`);
} finally {
  clearInterval(heartbeat); control?.close();
  if (child.exitCode === null && child.pid) {
    // Close only the fixture-owned process. A failure still cleans its process tree;
    // no user OBS process is discovered or killed by name.
    await exec('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      `$p = Get-Process -Id ${child.pid} -ErrorAction SilentlyContinue; if ($p) { [void]$p.CloseMainWindow() }`]).catch(() => {});
    const closed = await Promise.race([exited.then(() => true, () => true), delay(5000).then(() => false)]);
    if (!closed && child.exitCode === null) await exec('taskkill.exe', ['/PID', String(child.pid), '/T', '/F']).catch(() => {});
  }
  await f.close();
  await rm(websocketConfig, { force: true });
}
