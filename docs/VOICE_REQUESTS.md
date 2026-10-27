# Voice request lifecycle

This change makes request handling deterministic: a complete recognition result
is considered once, and a superseded search cannot start playback after a newer
local command has been admitted. It preserves the existing Client Credentials
setup, catalog ranking, explicit-content filtering, local Soloist playback and
ordinary Alice command classification.

This change is based on upstream `b02e066` and retains its corrected Quasar
f252/f253 cancellation frames, four cancellation methods, multi-device input
monitoring, dedicated-key behavior and Entware/library isolation. The exact-ID,
final-only recognition state replaces upstream's text/time-window deduplication.

## Before and after

| Sequence | Previous behavior | Updated behavior |
| --- | --- | --- |
| Partial `включи песню`, PROCESSING, then final `включи песню Don't Stop Me Now` | PROCESSING could launch the incomplete query and consume its request ID. | The partial causes no action. The final phrase reaches the existing classifier intact. |
| Partial `стоп`, then a longer final phrase | A keyword could dispatch before recognition finished. | Voice controls also wait for the explicit final flag. Physical controls remain available immediately. |
| Final request A, IDLE containing A, later a state update still containing A | Returning to IDLE cleared the deduplication cache and could replay A. | IDLE never dispatches; the recent request-ID cache survives IDLE and absent-state messages. |
| Request ID `abcdef`, then ID `abc` | Prefix comparison could suppress the second request. | ID length and every byte must match. |
| The same song phrase with two different request IDs | Text-only deduplication could suppress an intentional repeat. | Both IDs are accepted once. |
| A field-252 payload whose field-253 type is not AliceState | The payload could be interpreted as AliceState. | Wrapped payloads require type 3. Direct field-3 AliceState messages remain supported. |
| Search A is still waiting; a newer search B, pause, next or previous arrives | A could finish later and replace B or resume after pause. | The newer command invalidates A before its eventual playback dispatch. |
| Search returns 401, 403, 429, an unusable body, or no matches | Several error paths resumed the old queue. | Failed specific searches leave playback alone. A 429 also starts a shared cooldown. |
| Several requests need an expired API token | Each process could obtain a token independently. | API requests share a lock and recheck the cache after acquiring it. |

## Recognition boundaries

Only an explicitly final request/text result in a present, non-IDLE AliceState
can dispatch. The bridge never reads the retained `recognized_phrase` field as
a new request. `PROCESSING` by itself does not mean recognition is final.

The request ID comes from AliceState field 7. RequestState field 3 is a
`dialog_id`, which can span multiple recognitions; it is not used for request
deduplication. When a request has no ID, a fresh observed recognition cycle is
required before its final result can dispatch. A bridge that joins halfway
through an ID-less final result therefore ignores it rather than replaying a
possibly retained request. This conservative fallback may require repeating a
command on firmware that never supplies request IDs or cycle transitions.

The deduplication cache keeps 16 recent IDs of up to 128 bytes in memory; it is not a durable transaction
log. Restarting the bridge or replaying IDs after cache eviction is outside its
at-most-once boundary. Oversized IDs and phrases are rejected instead of being
truncated into a different identity or an incomplete song name. The existing
classifier accepts at most 507 bytes of recognized text; longer finals and text
containing an embedded NUL are ignored.

Weather, timers and household commands still reach the existing classifier.
The change adds no new interception rules. It also leaves the existing Quasar
TTS/cancellation wire messages unchanged. Final-result gating does **not** prove
that a particular firmware will suppress Alice's subscription speech; that
requires a physical-device acceptance check.

## Local command ordering

Bridge voice actions, short-touch controls, toggle and automatic next-track
actions use the shared command controller. Search, play, pause, next, previous
and toggle create a new request generation. Shuffle and repeat change playback
options without canceling a selection already in progress.

The bridge queues up to 16 commands and checks admission completion without
waiting in its event loop. Touch, Quasar and playback polling continue during
lock contention; queued commands enter the controller in event order. Queue
overflow is explicitly logged and the new command is rejected.

Automatic recovery has its own `auto-next` action. It carries the original
track URI and request generation observed before queueing. Only the bridge writes
the observed `track` record, containing the actual track URI and an observation
revision. The worker compares URI and generation under the dispatch lock and
discards stale recovery without incrementing the generation.

Before explicit track, album or artist playback, the controller snapshots the
observed record into `selection.pending`; only successful playback commits it
to `selection`. Recovery rejects the unchanged pre-play snapshot. A fresh
observation can arrive during or after the CLI call, and a context URI never
overwrites the actual observed track. Same-URI events publish a new revision only
while the active selection snapshot still matches the observed record; steady
position updates otherwise cause no writes. URI changes always publish.

Failed playback removes its pending marker without replacing the previous
selection. A guard preparation failure prevents play. If committing a successful
play's guard fails, the marker is retained, failure is logged, and automatic
recovery remains blocked until a later successful selection replaces it. Other
controllers cannot remove that marker as part of their own cleanup.

An observation revision identifies a fresh WebSocket observation, not a
player-correlated acknowledgement of the selection. This includes same-URI
context transitions: a periodic update cannot prove which command the player
has applied. No time-window heuristic or generation bump is used for this guard.
Recovery can advance the old track while an explicit search is
pending, but cannot invalidate that search. User `next` still cancels the search.

For a local control, the controller retains the admission lock and
keeps it until dispatch finishes. Two rapid next commands therefore both run
in order; a later shuffle-off cannot be overtaken by an earlier shuffle-on.
Search workers release admission before HTTP, allowing local controls to
invalidate a slow search.

The generation is checked again while holding the same local lock used to
dispatch playback. A control already sent to Soloist cannot be recalled. A
network request already sent to Spotify may finish, but its stale result is
discarded. Commands sent directly by an external Spotify Connect client or by
running `soloist ctl` outside these scripts do not participate in this ordering.

## Credentials and HTTP behavior

Keep the existing `spotify_api.conf` with `SPOTIFY_CLIENT_ID` and
`SPOTIFY_CLIENT_SECRET`. The helper still uses the Client Credentials grant;
this PR does not require a browser login, redirect callback or PKCE migration.
Playback continues through the existing Soloist session. No example playlist,
account token or private endpoint is required.

HTTP calls have a 5-second connection deadline and a 15-second total deadline and inspect both curl's exit
status and the HTTP status. TLS certificate verification remains enabled. A
failed explicit search does not issue a fallback `play` without a URI. Generic
`включи музыку` remains an intentional resume request.

A shared cooldown uses the integer number of seconds in Spotify's `Retry-After`
response, starting when the response arrives. A missing, malformed or unsupported
header uses a 60-second cooldown. HTTP-date headers are not parsed. New requests
during that interval fail without contacting the API; they are not queued for
surprise playback later. There are no randomized delays or attempts to disguise
automation. These measures reduce duplicate and obsolete work; they make no
promise about Spotify account restrictions, enforcement or eligibility.

There is no automatic retry loop. A 401 removes the cached API token; a later
explicit request may obtain a new one. Other HTTP errors and transport failures
stop that search. A successful response with no usable match may make the
existing one-time catalog-type fallback; it still cannot resume the old queue
without a selected URI. The existing small catalog extractor is retained, so
an incompatible response shape can produce no match rather than an automatic
parser migration.

## Installation and operation

The target needs its existing shell, curl and `flock` utility. The helper uses
only `flock -n` and `flock -u`; it does not require util-linux's `flock -w` or a
`timeout` command. Lock acquisition retries are bounded: 50 attempts with
0.1-second sleeps for local commands, and 600 for the API worker. Process
scheduling and command startup add overhead to these bounds. A superseded API
worker stops waiting as soon as it observes the newer generation.

Controller, search and toggle scripts retain upstream's system-first PATH and
clear inherited `LD_LIBRARY_PATH`/`LD_PRELOAD`. Only the actual Soloist child gets
the installation's `lib64` path; shells and HTTP helpers keep system libraries.

Local dispatch shares 27 polling intervals of 0.1 seconds across `now`,
`activate` and the final command, with 0.3 seconds reserved for ALSA and a
0.2-second TERM grace period before KILL if needed. This keeps a slow activation
plus a stalled play below the following controller's five-second admission
limit. A standalone status query has 30 intervals. The worker waits for its child to exit
before unlocking, including on HUP, INT or TERM. Signal handlers exit nonzero;
EXIT cleanup removes temporary responses. CLI children do not inherit either
lock descriptor. Scheduling and command startup add overhead to these nominal
bounds. Termination cannot undo a command already accepted by Soloist.

The bridge resets inherited HUP, INT and TERM dispositions in its child before
starting the controller. The controller reports admission through a nonblocking
pipe and then runs the worker in that same process; it does not start a shell
background job that would ignore SIGINT. The pipe closes after the admission
byte, and a controller that fails before admission closes it without a byte.
Neither case blocks the event loop. The bridge also reaps completed workers.

State belongs to one installation under `data/requests/`: generation number,
observed track/revision, selection snapshots, lock files, token/expiry, cooldown
and control diagnostics. New files are private to the running user
(`umask 077`). Closing a process releases its flock locks, including after a
crash. Do not remove lock files while any controller/search worker is active,
because a replacement file would represent a different lock.

The bridge uses the controller automatically. For the same ordering from a
local shell, use the supplied scripts:

```sh
cd /data/opt/soloist
sh scripts/control.sh pause
sh scripts/control.sh next
sh scripts/control.sh prev
sh scripts/control.sh search "включи песню Don't Stop Me Now"
sh scripts/control.sh shuffle on
sh scripts/control.sh repeat context
```

From a local shell, the controller runs in the foreground and returns the
worker's exit status. The bridge continues event polling while that process
runs and advances its FIFO as soon as admission is acknowledged. A foreground
search can also use `sh scripts/search_and_play.sh "включи песню Don't Stop Me Now"`. Both routes
share the same generation, token and cooldown. The existing
`/tmp/soloist-search.log` records recognized queries and outcome categories.
`data/requests/control.log` records failed action, stage and original exit
status (124 for a local timeout), including activation failures. Child responses,
token responses and Client Secrets are not included in these diagnostics.
The track guard uses the latest WebSocket state observed by the bridge; it does
not make external Spotify Connect controls part of the local transaction.

## Build and tests

Build the ARM64 bridge using the repository Makefile and install the matching
scripts together. Installing only the bridge or only the new scripts is not a
supported update. The installer copies the controller and lifecycle helpers.
The tracked ARM64 bridge binary must be rebuilt with the matching source because
the installer consumes it directly.

The host regression suite compiles the production parser/state helper:

```sh
python3 tests/test_voice.py
# Or use a portable Zig compiler:
python3 tests/test_voice.py /path/to/zig
```

The shell suite copies the scripts into a temporary installation and supplies
fake curl and Soloist executables. It makes no network requests, reads no real
credentials, and cannot change real playback:

```sh
sh tests/test_requests.sh
python3 tests/test_review.py
python3 tests/test_admission.py
```

The fixtures cover partial-to-final recognition, repeated finals around IDLE
and missing state, ID-length collisions, same text/new ID, wrapper type checks,
ID-less recognition cycles, HTTP and transport failures, shared search/token
rate-limit cooldown, concurrent token refresh, superseded searches, every
canceling local control, non-canceling shuffle/repeat, generic resume,
censorship, rapid local-command ordering and quoted song names. The upstream command classifier and
catalog ranking are reused; this is not a rewrite of either subsystem.

Physical acceptance should separately exercise a complete spoken song name,
pause while a search is slow, two distinct spoken selections, a short touch,
shuffle/repeat during a search, and an ordinary weather/timer question. Host
fixtures and an ARM64 build alone do not establish audio timing or firmware
behavior.

The review fixtures also cover TERM-ignoring local CLI termination/reaping,
post-timeout controls, automatic-recovery generation/track guards, all three
termination signals, redacted activation diagnostics, and nonblocking ordered
bridge admission using the production event-loop admission hook.
They also check that controller/HTTP helpers receive no library overrides and
only Soloist receives the bundled library path.
Context fixtures cover artist/album observations before and after CLI completion,
unchanged and fresh same-URI snapshots, failed play, retained guard commit errors,
and avoiding steady-state observation writes.
