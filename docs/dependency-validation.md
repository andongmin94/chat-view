# CHZZK dependency validation

Updated: 2026-09-19 (UTC). Scope: the P1-01b transport/preview integration gate, not a native rewrite or real-channel certification.

## What failed and what changed

At starting commit 0ac467b, Windows #276 passed but CHZZK contract #2 failed in all four jobs at dependency audit, before installed-library tests. The graph contained parseuri below 2.0.0 and socket.io-parser 3.3.4 with reported URI ReDoS and binary-attachment memory-exhaustion advisories.

The first attempted parseuri 2.0.0 override (e7ad608) was wrong: an advisory range boundary is not proof of a release. Registry metadata in contract #4 showed only 0.0.1-0.0.6 and 3.0.1/3.0.2. Failed install runs #3/#4 are not compatibility evidence. The temporary registry inspection step has been removed.

## Selected transport, not a parser compatibility layer

The public Socket.IO Manager and Engine.IO constructors accept object options. ChatView validates the API-issued NAVER URL, parses it using Node's standard URL, and supplies hostname, secure, port and decoded query fields to a fresh Manager. It never passes a string URI or the legacy `host` option. Socket.IO framing/handshake remain implemented by the library.

Socket.IO-client 2.0.3 is retained for CHZZK's documented range, using engine.io-client 3.5.6 and socket.io-parser 3.3.6. The old parseuri dependency is replaced by published 3.0.2. **3.0.2 has a different ES-module API and is not a callable drop-in replacement.** Neither ChatView nor its selected Manager/Engine.IO path calls that API. No parser shim, source patch, production module-cache mutation or fallback URI path is added. Do not change construction back to io(url) or new Manager(url).

The actual-library fixture negotiates EIO3 over WebSocket, verifies the options-only constructor, preserves an encoded test ticket with plus/slash/percent/Unicode, receives a CHAT event, and explicitly unsubscribes. The test-only constructor changes the destination to a loopback server; production has no arbitrary-endpoint option. This verifies the installed stack, not NAVER's own endpoint, TLS behavior against that endpoint, or commercial API approval.

## Dependency lock and evidence

Contract #5 at 12470eb successfully installed the graph, reported **zero audit findings**, and passed **82 tests**, including actual transport and authenticated local preview. Its strict type check found two narrowing diagnostics in the test's captured HTTP address. That is corrected by capturing the numeric port after validation, not suppressing strict checking.

The committed package-lock.json contains the actual resolver graph reported by the Linux/Node24 job of #5, with original versions, URLs, integrity values and dependency metadata retained (JSON whitespace compacted). No integrity hashes were invented. Both runtime and development dependencies are audited at low severity without exemptions. CI now uses npm ci and checks that installation does not alter either manifest. The lock must pass the new commit's four clean-install jobs before this gate is complete.

Known-good native source remains a7ea2e7. No src/, native tests, CMake, installer, privacy policy or Windows workflow changes are part of this dependency work. Real account/channel authorization, native first-party HUD integration, OBS-free dual-PC video and advertising accounting are still separate gates.

## Sources and run references

- Original failed audit: https://github.com/andongmin94/chat-view/actions/runs/35449471052
- Registry version evidence: https://github.com/andongmin94/chat-view/actions/runs/35450955155
- Resolver/82 tests/audit and test-only type failure: https://github.com/andongmin94/chat-view/actions/runs/35451316782
- CHZZK protocol: https://chzzk.gitbook.io/chzzk/chzzk-api/session
- Manager object options: https://github.com/socketio/socket.io-client/blob/2.0.3/lib/manager.js
- Engine.IO object options: https://github.com/socketio/engine.io-client/blob/3.5.6/lib/socket.js
- parseuri API: https://github.com/slevithan/parseuri/blob/main/src/index.js
- Parser advisory: https://github.com/socketio/socket.io/security/advisories/GHSA-2m8v-j782-fhvr
- URI advisory: https://github.com/advisories/GHSA-6fx8-h7jm-663j
