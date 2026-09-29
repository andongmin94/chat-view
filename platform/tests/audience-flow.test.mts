// SPDX-License-Identifier: GPL-2.0-or-later
// Actual HTTP/SQLite/WS plus the real CHZZK HTTP adapter; provider responses and
// OBS reports are synthetic. This never contacts a live channel or creates pay.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { randomBytes } from 'node:crypto';
import { once } from 'node:events';
import { setTimeout as pause } from 'node:timers/promises';
import { WebSocket } from 'ws';
import { ChzzkApi } from '../server/chzzk/api.mts';
import { SessionStore, hashSecret } from '../server/chat/session-store.mts';
import { ProviderGrants } from '../server/service/provider-grants.mts';
import { Creators } from '../server/service/creators.mts';
import { PlatformApplication } from '../server/service/application.mts';
import { DisplayGateway } from '../server/chat/display-gateway.mts';
import type { ChatSnapshot } from '../server/chzzk/session.mts';
function deferred<T>() { let resolve!: (v:T)=>void; const promise=new Promise<T>(yes=>{resolve=yes;});return {promise,resolve}; }
const ok=(content:unknown)=>Response.json({code:200,content});
const page=()=>ok({data:[{channelId:'alice',liveId:123,concurrentUserCount:17}],page:{}});

async function fixture(enabled=true) {
  let origin='', liveCalls=0, pageHook:()=>Promise<Response>=async()=>page();
  const api=new ChzzkApi({clientId:'fixture',clientSecret:'PRIVATE_CLIENT'},async(url,init)=>{
    if(url.includes('/open/v1/lives?')) {
      liveCalls++; assert.equal(new Headers(init.headers).get('Client-Id'),'fixture');
      assert.equal(new Headers(init.headers).get('Authorization'),null); return pageHook();
    }
    if(url.endsWith('/auth/v1/token')) {
      const body=JSON.parse(String(init.body));
      return ok({accessToken:`access:${body.code}`,refreshToken:`refresh:${body.code}`,tokenType:'Bearer',expiresIn:'3600'});
    }
    if(url.endsWith('/users/me')) {
      const owner=new Headers(init.headers).get('Authorization')!.split(':')[1]!;
      return ok({channelId:owner,channelName:owner});
    }
    return ok(null);
  });
  const sessions=new SessionStore(),grants=new ProviderGrants(sessions.database,randomBytes(32));
  const creators=new Creators({api,sessions,grants,audienceApi:enabled?api:undefined,
    createDisplay:(access,get)=>new DisplayGateway(access,()=>origin,get),
    createChat:changed=>{
      let snapshot:ChatSnapshot={state:'idle',received:0,messages:[]};
      return {snapshot:()=>snapshot,async start(){snapshot={state:'subscribed',received:0,messages:[]};changed();},
        async stop(){snapshot={state:'stopped',received:0,messages:[]};changed();return true;}};
    },
  });
  let app:PlatformApplication;
  const server=createServer((req,res)=>{void app.handle(req,res);});
  server.on('upgrade',(req,socket,head)=>app.upgrade(req,socket,head));
  await new Promise<void>(resolve=>server.listen(0,'127.0.0.1',resolve));
  const address=server.address();assert(address&&typeof address!=='string');origin=`http://127.0.0.1:${address.port}`;
  app=new PlatformApplication(creators,origin,state=>`https://chzzk.naver.com/account-interlock?state=${state}`);
  const request=(path:string,init:RequestInit={})=>fetch(origin+path,{...init,redirect:'manual',signal:AbortSignal.timeout(5000)});
  type Browser={cookie:string;csrf:string};
  const get=async(path:string,b:Browser)=>{
    const res=await request(path,{headers:{Cookie:b.cookie}});assert.equal(res.status,200);
    const html=await res.text();b.csrf=/name="csrf" value="([a-f0-9]{64})"/u.exec(html)?.[1]??b.csrf;return html;
  };
  const post=(path:string,b:Browser)=>request(path,{method:'POST',headers:{Cookie:b.cookie,Origin:origin,
    'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({csrf:b.csrf})});
  const native=(path:string,scheme:string,token:string)=>request(path,{method:'POST',headers:{Authorization:`${scheme} ${token}`,'X-ChatView-Role':'streaming'}});
  async function connect(owner:string) {
    const verifier=randomBytes(32).toString('hex');
    const started=await native('/display/login','ChatView-Challenge',hashSecret(verifier));assert.equal(started.status,200);
    const {id,verificationPath}=await started.json() as {id:string;verificationPath:string};
    const landing=await request(verificationPath);assert.equal(landing.status,303);
    const b={cookie:landing.headers.get('set-cookie')!.split(';')[0]!,csrf:''};await get(verificationPath,b);
    const login=await post(`/login/${id}/connect`,b);assert.equal(login.status,303);
    const state=new URL(login.headers.get('location')!).searchParams.get('state');
    const callback=await request(`/callback?state=${state}&code=${owner}`,{headers:{Cookie:b.cookie}});
    assert.equal(callback.status,303);b.cookie=callback.headers.get('set-cookie')!.split(';')[0]!;
    await get(verificationPath,b);assert.equal((await post(`/login/${id}/approve`,b)).status,200);
    const polled=await native(`/display/login/${id}`,'ChatView-Login',verifier);assert.equal(polled.status,200);
    const {lease}=await polled.json() as {lease:{token:string;sessionToken:string;outputToken:string}};
    await get('/campaigns',b);assert.equal((await post('/campaigns/chatview-test/select',b)).status,303);
    const peer=new WebSocket(origin.replace('http:','ws:')+'/display/events',{headers:{Authorization:`Bearer ${lease.token}`}});
    peer.on('error',()=>{});await once(peer,'open');
    let sequence=0;
    const report=()=>request('/broadcast/output',{method:'POST',headers:{Authorization:`ChatView-Output ${lease.outputToken}`,
      'Content-Type':'application/json'},body:JSON.stringify({sequence:++sequence,streaming:true,recording:false,sampleAgeMs:0})});
    return {b,lease,peer,report};
  }
  return {creators,request,get,post,native,connect,get liveCalls(){return liveCalls;},set(hook:typeof pageHook){pageHook=hook;},
    async close(){app.close();server.closeAllConnections();await creators.close();await new Promise<void>(resolve=>server.close(()=>resolve()));grants.close();sessions.close();}};
}

test('official sample -> accepted report estimate -> private owner page, without blocking the report or adding public authority', {timeout:15000},async()=>{
  const f=await fixture(),pending=deferred<Response>();
  try {
    f.set(()=>pending.promise);const alice=await f.connect('alice'),bob=await f.connect('bob');
    for(let i=0;i<5;i++) await f.get('/campaigns/activity',alice.b);
    assert.equal(f.liveCalls,0,'account reads cannot start provider sampling');
    assert.equal((await alice.report()).status,200,'report must return while provider is unresolved');assert.equal(f.liveCalls,1);
    await pause(550);assert.equal((await alice.report()).status,200);assert.equal(f.creators.activity.summary('alice').estimatedViewerMs,null);
    pending.resolve(page());await f.creators.audience.refresh();
    await pause(550);assert.equal((await alice.report()).status,200);
    await pause(550);assert.equal((await alice.report()).status,200);
    const summary=f.creators.activity.summary('alice');assert(summary.estimatedViewerMs!>0);
    assert.equal(summary.estimatedViewerMs,17*summary.audienceCoverageMs);assert(summary.audienceUnmeasuredMs>0);
    const html=await f.get('/campaigns/activity',alice.b);assert.match(html,/17명/u);assert.match(html,/시청자·분/u);
    for(const secret of [alice.lease.token,alice.lease.outputToken,alice.lease.sessionToken,'PRIVATE_CLIENT']) assert(!html.includes(secret));
    assert.equal(f.creators.activity.summary('bob').estimatedViewerMs,null);
    assert.doesNotMatch(await f.get('/campaigns/activity',bob.b),/17명/u);
    assert.equal((await f.request('/campaigns/activity')).status,401);
    assert.equal((await f.request('/campaigns/activity',{headers:{Authorization:`Bearer ${alice.lease.token}`}})).status,401);
    assert.equal(f.liveCalls,1);assert.equal(alice.peer.readyState,WebSocket.OPEN);assert.equal(bob.peer.readyState,WebSocket.OPEN);
    const before=summary.estimatedViewerMs;await f.get('/campaigns',alice.b);await f.post('/campaigns/stop',alice.b);
    await pause(550);await alice.report();assert.equal(f.creators.activity.summary('alice').estimatedViewerMs,before);
    assert.equal((await f.native('/display/signout','ChatView-Session',alice.lease.sessionToken)).status,200);
    assert.equal(f.creators.audience.current('alice').state,'unavailable');
  } finally {pending.resolve(page());await f.close();}
});

test('quota failure leaves counts unmeasured and chat alive, never refreshes a user grant or retries per report', {timeout:10000},async()=>{
  const f=await fixture();
  try {
    f.set(async()=>new Response('private quota body',{status:429}));const alice=await f.connect('alice');
    assert.equal((await alice.report()).status,200);await f.creators.audience.refresh();
    await pause(550);assert.equal((await alice.report()).status,200);
    assert.equal(f.liveCalls,1);assert.equal(alice.peer.readyState,WebSocket.OPEN);
    const html=await f.get('/campaigns/activity',alice.b);
    assert.match(html,/제공자 호출 제한/u);assert.doesNotMatch(html,/private quota body/u);
    assert.equal(f.creators.activity.summary('alice').estimatedViewerMs,null);
  } finally {await f.close();}
});

test('disabled production collection preserves existing activity without any live-list traffic', {timeout:10000},async()=>{
  const f=await fixture(false);
  try {
    const alice=await f.connect('alice');await alice.report();await pause(550);await alice.report();
    assert.equal(f.liveCalls,0);assert(f.creators.activity.summary('alice').activeMs>0);
    assert.match(await f.get('/campaigns/activity',alice.b),/수집 비활성/u);
    assert.equal(alice.peer.readyState,WebSocket.OPEN);
  } finally {await f.close();}
});
