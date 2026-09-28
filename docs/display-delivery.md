# Private-display delivery contract

Updated: 2026-09-28. [development-plan.md](development-plan.md) owns evidence/priorities, [native-display-client.md](native-display-client.md) the Windows consumer and [PRODUCT.md](../PRODUCT.md) the product scope.

## Authority and login

The HTTPS application composes creator-isolated provider sessions, browser login, display access and gateway. One creator's upstream serves its approved displays. No hardware registration, copied pairing ID or manual-key UX exists. Native renewals, chat bearers, browser cookies, provider grants and output-report capabilities are separate authorities. A logical session is not a measured broadcast interval or viewer identity.

| Operation | Authentication and effect |
| --- | --- |
| `POST /display/login` | `ChatView-Challenge <sha256(verifier)>`, mandatory singular `X-ChatView-Role: gaming` or `streaming`, optional `X-ChatView-Remember: 1`. Role is fixed before the browser opens. |
| Browser `/login/<id>` | Provider authentication resolves the creator; explicit confirmation approves the channel and fixed role. Streaming consent describes output-state reporting. Old role-selector URLs are removed. |
| `POST /display/login/<id>` | `ChatView-Login <verifier>` collects one-use approval, bound to creator/authorization generation/role. Landing URL alone is insufficient. |
| `POST /display/refresh` | `ChatView-Session <renewal>` plus role header. Wrong role is 409 before replacing a lease; roleless approvals are not upgraded. |
| `POST /display/signout` | Revokes the submitted ChatView approval, not all provider tokens. Native logout uses its actual in-run approval with or without persistence. |
| Upgrade `/display/events` | `Bearer <chat token>`; receive-only WebSocket, one connection per lease. Application messages terminate it. |
| `POST /broadcast/session` | Native renewal credential returns only its membership and `captureState: unverified`; not output-writing authority. |
| `POST /broadcast/output` | `ChatView-Output <output token>` and a tiny JSON report. Only an authorized live streaming display lease can report. |
| Developer-only `/display/exchange` | Internal one-use ticket exchange for fixtures/local probe, never exposed by the public service. Not a manual-key UX or compatibility layer. |

Every explicit approval receives in-run renewal. The remember choice controls only Windows DPAPI restart persistence. Native secrets are hashed in the server database; provider grants are separately encrypted. Chat and output reports are not persisted. The public service must match the native build's protocol; old ABI/wire migration is not provided.

Native logout clears rendering immediately and cancels the reporter, then performs matching local cleanup and server revocation on its worker. It requires acknowledgement before claiming success. Ordinary stop/exit and lost responses are not confirmed logout. A failed logout may be explicitly retried within the process; unrelated saved accounts are not substituted or deleted. D5 is closed in the plan.

## Output observation and report scope

A streaming login/renewal also returns `outputToken` and `outputScope: "broadcast:report"`. The token is distinct from the short chat bearer and long native renewal, is stored as a hash on the same lease, and shares its expiry/revocation. Gaming approvals never receive it. It is memory-only in the native worker, never in provider configuration, OBS callbacks, URLs, browser-management pages or the chat document.

The sender reads the existing OBS frontend's streaming/recording-active values via local shared memory. IPC version 5 retains 64-byte size and atomically packs a monotonic sampled time plus two booleans. A live parent, valid snapshot and no shutdown are required; stale/future/absent observations are not freshened by reading them. Frontend EXIT clears the observation and stops polling rather than calling the frontend API after exit.

An independent cancellable WinHTTP reporter prevents slow reporting from blocking the chat receiver or OBS/UI callbacks. It reports changed values at most once a second and refreshes unchanged observations approximately every five seconds. A source sample must be less than three seconds old. Reports have exactly these fields:

```json
{"sequence":1,"streaming":true,"recording":false,"sampleAgeMs":0}
```

Sequence is a positive 32-bit integer and strictly increases within a lease. The service rejects out-of-order/replayed reports, intervals below 500 ms, non-boolean states, old ages, unknown fields, non-JSON or bodies larger than 1 KiB. The body read is bounded to three seconds and its duration counts against sample age. It rechecks authority after reading: a concurrent logout, lease rotation or provider revocation cannot be undone by late body completion. No client-selected owner/session, scene name, capture claim, audience or financial field is accepted.

The server keeps only one non-durable recent observation per creator gateway. A corresponding open streaming display lease is required both at acceptance and display. Expiry, lease replacement, disconnect, logout, provider revocation and service restart give **unknown**, never an inferred stop. A failed report does not terminate otherwise authorized private chat. These are recent client reports, not remote OBS attestation, verified end-to-end broadcast delivery, capture safety or billable exposure. Recording-active does not prove an unpaused/increasing file. Replay buffer, virtual camera, scene state and durable broadcast history are not reported here.

## Recipient data and bounds

Each gateway frame sanitizes chat with `nativeChatSnapshot` and supplies recipient-specific connection metadata. Example identifiers below are synthetic, not credentials:

```json
{
  "type":"chat-snapshot","version":1,
  "snapshot":{"state":"subscribed","received":0,"messages":[]},
  "connection":{
    "role":"gaming",
    "broadcastSessionId":"01234567-89ab-cdef-0123-456789abcdef",
    "connectionId":"abcdef01-2345-6789-abcd-ef0123456789",
    "gamingConnections":1,"streamingConnections":1,
    "captureState":"unverified",
    "output":{"state":"reported","streaming":true,"recording":false,"expiresInMs":14000}
  }
}
```

Without fresh authorized evidence `output` is exactly `{"state":"unknown"}`. Reported lifetime is at most 15 seconds, reduced by sample age and elapsed server time. The native receiver validates the object, bools, bounded integer lifetime and presence of a streaming connection. It conservatively accounts for frame waiting time and clears expired output independently of new frames. The native connection panel labels the value **송출 PC 보고** and always retains **영상 제외 미검증**. Output/capture unknown does not itself suppress private chat; existing independent privacy protections still apply.

Connection counts deduplicate approved open display connections in that creator/session; they are not audience counts. Half-open transports can remain counted until retired. A recipient never receives another creator's report. Membership must equal its approved role/session/connection; identifier syntax/counts and `captureState: unverified` are validated. The entire `connection` object is stripped before WebView2 sees the chat envelope.

The immutable chat renderer gets only bounded state/count and nickname/content/time. No provider secrets, output tokens, membership, window commands or accounting authority enters it. Frames remain at most 2 MiB UTF-8/100 rows; ws owns framing. Existing 50 ms coalescing/five-second pulses and slow-consumer disconnection remain. Native fifteen-second chat liveness and lease watchdogs clear stale delivery. OBS capture/visibility policy is not changed by output reporting.

## Transport and verification

Actual TLS is mandatory except explicit literal-loopback development. Host/Origin/cross-site/method/body/duplicate-header/scheme checks remain; forwarded headers cannot authorize plaintext. Cookies and automatic HTTP authentication/redirects are disabled in the native client. Async send payload storage is retained through handle closing, including cancelled reporting requests. No public manual ticket route or alternative protocol fallback is added. The developer probe does not expose the public service's output-report route; its chat fixtures explicitly report unknown output.

Contract tests exercise real local HTTP/WebSocket and SQLite with synthetic provider input, including scope separation, renewal/revoke fences, order/rate/expiry and account isolation. Windows tests separately exercise actual IPC reader, WinHTTP and native UI, report delay without chat interruption, stale producer and logout. Their synthetic frontend observations do not certify a live OBS stream, physical two-PC topology, deployed TLS service, clean video or financial exposure. Exact outcomes belong only in the development plan.
