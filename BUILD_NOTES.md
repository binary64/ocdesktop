
## 2026-06-06 — AppImage packaging trap (build 5 signing)
SYMPTOM: signed AppImage launched into stock Telegram phone-login (no gateway).
CAUSE: re-signed from squashfs-root/ WITHOUT copying the freshly-built binary in.
  squashfs-root/usr/bin/ocdesktop was a STALE Jun-5 binary (pre-seeder code).
FIX/RULE: after every `ninja Telegram`, BEFORE signing:
  1. cp out/Release/Telegram /tmp/x && strip /tmp/x && cp /tmp/x squashfs-root/usr/bin/ocdesktop
  2. verify seeder present: strings squashfs-root/usr/bin/ocdesktop | grep "reattached live gateway"
  3. appimagetool --sign --sign-key DB6A8A47... squashfs-root dist/...AppImage
  4. VERIFY EMBEDDED: extract usr/bin/ocdesktop back out of finished AppImage,
     sha256 must match the freshly-stripped binary. NEVER trust the AppDir blindly.

## SHIP GATE (standing rule from James, 2026-06-07)
QA the full REAL path yourself before sending any AppImage — headless drive,
wait for the picker, click the actual button, verify seed logs + screenshots +
app-still-alive. Only send when QA passes. James accepts skipped build numbers:
a number he never receives = a build that failed QA and was rebuilt, not shipped.
Never ship on assumption (build 10 shipped a crash because QA bypassed the click
path via an env-var shortcut — don't repeat that).

## 2026-06-07 — build 13: System picker member + LIVE-PUSH (incoming-msg fix)
SYSTEM BUTTON: bridge roster() appends synthetic {"id":"system","label":"System"};
list_sessions(user="system") filters to sessions whose user_id is NOT in the
named-roster set (legacy NULL + any unnamed id). Static KnownUsers() fallback also
gains a System entry. ~757 such sessions live.

INCOMING-MESSAGES BUG (was: "new messages from the session don't come through"):
ROOT CAUSE — bridge only pushed updates from INSIDE _handle_send. Any message
arriving by another route (Telegram, cron, proactive agent output) lands in the
session DB but was never pushed → invisible on an open client.
FIX — per-connection async poller poll_new_messages() in ws_handler: keeps a
per-peer watermark dict (peerId→last pushed msg id), every 2s calls
bridge.messages_after(peer, watermark) (reads DB straight, source-agnostic) and
emits {"op":"update","kind":"message.new",...} for each new row, advancing the wm.
Watermark seeded from sessions.list dialogs' topMessageId; _handle_send advances
it past the DB max after a streamed turn so the poller doesn't double-render the
reply it already streamed under synthetic ids. Poller task created on first
sessions.list, cancelled on socket close.
THIS PUSH RAIL is what the browser-control tool (next build) rides on: a new
update kind pushed the same way → client acts on it.
VERIFIED end-to-end: append_message() out-of-band into a live session → ws client
got message.new in <2s, correct peer (test harness in session, message deleted
after). Picker renders James/Abi/System on the real build-13 binary; System seeds
50 dialogs/235 msgs (env-pinned OCDESKTOP_HERMES_USER=system path; headless click
on the 3rd modal button wouldn't land under matchbox — coordinate drift, not a
bug. The James-click callback path was already proven in build 12 QA).
Shipped build13 AppImage sha 4d911846..., embedded binary 27efffb9... verified ==.
STALE-BINARY TRAP RECURRED: appimagetool packed the OLD build-12 binary still in
squashfs-root (sha 24684ee3) on first roll — caught by verify-back (mismatch vs
fresh 27efffb9), re-copied fresh binary, re-rolled, re-verified ==. ALWAYS cp the
fresh stripped binary into squashfs-root/usr/bin/ocdesktop BEFORE packing.

## NEXT BUILD (planned, build 14) — embedded browser trio
James wants: (1) replace the right-panel Info section with an embedded web
browser, (2) a Hermes tool/MCP to control it (navigate URL), (3) on convo first
load, scan history for the last URL set and reload it.
FEASIBILITY (checked): tdesktop ships its OWN webview — Telegram/lib_webview,
Webview::Window (webview_embed.h) with .navigate(url)/.reload()/.eval(). Uses
webkit2gtk on Linux → NEEDS A REAL DISPLAY; returns "Could not initialize WebView"
under headless Xvfb. So the rendered page CANNOT be self-QA'd headless — only the
panel swap + navigate-command flow are headless-testable; James must eyeball the
actual render on his box. Build it to degrade gracefully (fallback label, not a
crash) if webkit is missing.
ARCH: navigate commands come down the SAME push channel as message.new — add a
new update kind (e.g. "browser.navigate") the client routes to the webview.
URL-restore: the Hermes tool's navigate calls get persisted to the session (so
they show in history); on convo open, scan back for the last one and navigate.
Info section seam: showSection(Info::Memento) in window_session_controller.cpp —
that's the panel to replace with the webview widget.

## ROSTER NOW DYNAMIC (done 2026-06-07, build 12)
WAS hardcoded in ConnectConfig.h KnownUsers(). NOW: bridge has a `roster` op —
distinct user_ids derived live from the sessions table (SELECT user_id ... GROUP
BY), display names from the OCDESKTOP_ROSTER env ("id:Name,id:Name", set in
~/.hermes/ocdesktop-ws.env), fallback to raw id. Client: WsGateway::
fetchRosterBlocking() does a lightweight auth+roster WS round-trip; StartConnectFlow
fetches it before showing the picker and passes the list to UserPickerBox(members,
...). KnownUsers() kept ONLY as offline fallback when the fetch fails/returns empty.
Adding a 3rd member = one OCDESKTOP_ROSTER env line + restart ocdesktop-ws, NO
client rebuild. Bridge change is hot (service runs from repo file).
QA'd headless: "fetched 2 roster members from bridge" → picker James/Abi → click
seeded user 0000000001 clean (docs/build12-*.png). Shipped build12 AppImage
sha d3e0e3e7..., embedded binary sha 24684ee3... verified ==.

## 2026-06-07 — build 11: click-James crash (use-after-free in picker callback)
SYMPTOM: clicking James/Abi in the picker did "nothing" — app crashed (SIGSEGV)
  or fell back to the stock Telegram intro screen. Reproduced headless: click →
  seed runs → segfault → black screen.
CAUSE: UserPickerBox click callback ran `chosen(id)` (synchronous seed of 50
  dialogs/1058 msgs + intro→main window swap via showAccount) and THEN called
  `box->closeBox()` on a box whose parent layer was torn down mid-callback.
FIX: closeBox() FIRST, then defer `chosen(id)` to a fresh main-loop tick:
    button->setClickedCallback([=]{ box->closeBox(); crl::on_main([=]{ chosen(id); }); });
  crl::on_main must be UNGATED (not tied to `box` lifetime) or the deferred seed
  gets cancelled when the box dies → silent no-op. Needs #include <crl/crl_on_main.h>.
QA HARNESS PITFALLS (cost hours):
  - Build container: run as `-u 0` (rootless docker maps container-root→host-arthur).
    `-u $(id -u)` makes host files appear root-owned inside → permission denied.
  - Build tree cache is keyed to mount path: mount at /usr/src/tdesktop (not /tdesktop),
    target `cmake --build out` (NOT out/Release — that's just the binary dir).
  - Each headless run MUST use a FRESH -workdir (/root/ocd_$$_$RANDOM). Reusing
    /root/ocddata hits the single-instance socket lock → instance quits instantly,
    logs only log_start0.txt "sending show command... activating and quitting".
  - Poll the app log for "OpenClaw connect: showing user picker" before clicking —
    the picker only appears AFTER the WS connect resolves (flaky/slow in container,
    ~5-20s). Clicking too early hits the intro's "Start Messaging" button instead.
  - WS bridge binds ONLY to the tailnet IP (100.99.160.15:8770), not 127.0.0.1.
  - Container needs wmctrl+xdotool+imagemagick (baked into ocd:test2 image).
  - James button center = (640,473) at 1280x900 with matchbox WM (win 818x642@231,118).
