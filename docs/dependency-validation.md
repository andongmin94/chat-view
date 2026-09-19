# CHZZK dependency validation

Updated 2026-09-19. This is a bounded blocker to P1-01b, not another native rewrite.

At starting commit `0ac467b1ed47ff06a30249adaa50c35d3c045250`, Windows #276 passed but CHZZK contract #2 failed in all four jobs at the dependency audit. The socket and preview tests therefore had NOT run against the installed libraries. The prior local mock tests did not establish library compatibility.

Audit evidence: https://github.com/andongmin94/chat-view/actions/runs/35449471052

The installed graph included vulnerable parseuri versions below 2.0.0 and socket.io-parser 3.3.4. Use upstream parser 3.3.6 and parseuri 2.0.0 throughout the graph while retaining the documented Socket.IO-client 2.0.3 / Engine.IO 3 transport. No custom protocol implementation, patched vendor source, protocol fallback or audit exception is introduced. The audit now covers development dependencies too. This choice must pass the actual wire tests and be qualified against a real authorized CHZZK channel; it is not NAVER certification.

The wire test checks the resolved client and transport copies of parseuri, and now preserves the API-issued URL query when redirecting to the local fixture. It verifies that the auth query reaches the fixture through the actual client/parser/transport stack rather than silently dropping the ticket in the test.

The complete resolved manifest is reported even when a later check fails. Commit the successful resolver output as package-lock.json and switch installation to npm ci before closing this gate. Do not manufacture hashes or silently remove audit failures. The build job has no repository write permissions.

Sources checked 2026-09-19:

- https://chzzk.gitbook.io/chzzk/chzzk-api/session
- https://github.com/socketio/socket.io/security/advisories/GHSA-2m8v-j782-fhvr
- https://github.com/advisories/GHSA-6fx8-h7jm-663j

Current status of this change: library versions updated; new remote CI and locked clean-install verification pending. No real channel credentials, native first-party HUD, dual-PC clean-feed or ad accounting evidence is claimed.
