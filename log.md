# ocdesktop — Log

## 2026-05-29
- Project scaffolded into unified project-OS skeleton.

## 2026-05-29 (quick scan)
- Card: "Review charter, set first real task". Charter was an empty stub.
- Identified project from memory: binary64/ocdesktop = James's C++/Qt tdesktop fork → native OpenClaw desktop client (not the Electron app, not desktop-portal web portal).
- Populated HERMES.md (what/why/status), seeded kanban + tasks with 3 real backlog cards.
- Known CI blocker recorded: self-hosted GHA runner on Jupiter fails (docker.sock not a socket; RKE2/containerd). PR #1 fix/linux-cmake-qt-private noted.
- Next step: verify PR #1 current status on next deeper cycle (research only).

## 2026-06-04 — upstream catch-up v6.6.4 → v6.8.4

**Branch model (IMPORTANT):**
- `telegramdesktop/tdesktop` = PULL-ONLY upstream. NEVER push/PR/change anything against it. All pushes/PRs → `binary64/ocdesktop` (origin).
- Enforced at git level: `upstream` remote push URL hard-set to `DISABLED_PULL_ONLY` so `git push upstream` physically can't reach Telegram's repo.
- Upstream's REAL default branch is **`dev`** (their HEAD). `upstream/master` is STALE (stuck at v6.1.1) — IGNORE it; comparing against master is meaningless. Always track/compare against `dev`. Telegram tags stable releases (`vX.Y.Z`) — pull the **stable tag**, not bleeding-edge dev.

**What was done:**
- Added `upstream` (fetch-only), fetched, merged stable tag **v6.8.4** into `main` (1,129 commits).
- Conflicts were **only 5 submodule gitlinks** (lib_base, lib_ui, lib_webrtc, lib_webview, cmake) — ZERO source-file conflicts. Version file + CI workflows auto-merged clean. Resolved all submodules to upstream's (theirs/stage-3) commits, then `git submodule update --init`.
- tgcalls fetch error during recursive auto-fetch was a stale-ref red herring — resolved fine once correct gitlink was checked out. `cmake` shows ` m` in status due to an uninitialised NESTED submodule (external/glib/cppgir) — harmless, initialised at build-config time.
- Pushed `main` to origin (binary64): `ad078ed..e52c9b5`.
- Safety net: `backup/main-pre-6.8.4` branch preserves pre-merge state for rollback.

**State after merge:**
- `main` = **v6.8.4**, +4 ahead of v6.8.4 tag (our fork commits: 2 version-catchup squashes + 2 dependabot CI bumps), −44 behind `upstream/dev` (post-release dev churn we deliberately skipped; all cosmetic/UI/build-fix, none touch MTProto/gateway seam).
- vs `upstream/master`: +3008 / −0 (master is stale, ignore).

**6.8.2 → 6.8.4 delta highlights:** Qt **6.11.1** bump (6.8.3) — Linux build deps in `../Libraries` must match or build fails. Security fix: passcode bypass via separate windows while locked (6.8.4). Plus drag-drop photos, native IV, Linux webapp viewer, screen-reader support, in-app markdown viewer. None touch the protocol seam.

**Still pending (next session):**
- `feat/mtproto-removal` is STILL on old v6.6.4 base — needs rebase onto new v6.8.4 `main`. Real protocol-layer conflicts will surface there (the 6.7→6.8.0 churn is the friction, now absorbed into main). PR #3 was CLOSED (paused); branch preserved.

## 2026-06-04 (later) — moved main onto dev + cruft cleanup

- Pulled **upstream/dev** (44 commits past v6.8.4) into `main` — clean merge, ZERO conflicts (incl. submodules). main now tracks dev's bleeding edge, not just the stable tag. Backup: `backup/main-pre-dev`.
- Audited our "+5 ahead of dev": found the only real content divergence was **cruft from the original v6.6.4 squash**:
  - `Telegram/build/docker/centos_env/Dockerfile.resolved` (883 lines) = **generated** file (output of gen_dockerfile.py), NOT tracked upstream. Accidentally committed. → **deleted**.
  - `Telegram/lib_crl` gitlink was **stale by ~3 months** (our pointer `a41edfc` was a strict ANCESTOR of upstream's `f770e4e`). main reported "0 behind dev" but this one submodule silently lagged. → **bumped to upstream's pointer**.
  - GOTCHA for future merges: squash-style "pull upstream" merges can leave individual submodule gitlinks behind even when the superproject looks in sync. Verify `git diff --stat upstream/dev...main` is EMPTY after a catch-up, not just the commit count.
- Commit `26cd145` (cleanup), pushed origin `2b5683d..26cd145`.
- **RESULT: `main` is now a clean mirror of `upstream/dev`** — `git diff upstream/dev..main` is empty. Remaining commits on main are merge-mechanism ("pull upstream" x3) + 1 dependabot CI bump; no separable fork features. All real fork work lives only on `feat/mtproto-removal`.

## 2026-06-04 (later) — Hermes gateway PR #5 + mock layer + builds

**Decisions locked (James):** Linux AppImage only. Several mock sessions, NO live reply. CI build cap 3h. Self-hosted runner fix cap 2h. GitHub-hosted runners OK.

**Done + VERIFIED (compiled & executed in Qt6 container):**
- Fresh branch `feat/hermes-gateway` off clean `main` (PR #5, draft). Superseded `feat/mtproto-removal`.
- `openclaw/GatewayInterface.h` (carried + fixed: use std::uint64_t etc, not tdesktop's uint64 aliases — header now self-sufficient).
- `openclaw/MockGateway.{h,cpp}` — in-memory impl. Loads fixtures from $OCDESKTOP_MOCK_JSON / AppData / bundled `:/openclaw/mock.json`. No network, no backend reply, self-echoes outgoing. VERIFIED: loads 5 sessions, parses history, all callbacks fire, exit 0 — both via env-override AND via rcc-compiled bundled resource.
- `Resources/openclaw/mock.json` — 5 sessions (Arthur, Hermes, Sam-Powerlinks, Garden, Infra) + scripted history.
- `Resources/qrc/openclaw.qrc` — bundles mock.json so AppImage is self-contained.
- CMake: openclaw sources + qrc wired into Telegram target.
- `.github/workflows/appimage.yml` — ubuntu-latest, 180min cap, centos_env container Release build (TDESKTOP_API_TEST=ON, no real TG creds), linuxdeploy+qt → AppImage artifact.
- Disabled stock fork workflows (mac/win/snap/linux/docker) — were wasting Actions minutes on every push. Cancelled 24 stray runs.

**In flight:** GHA AppImage build (run 26930056370) + local podman libraries build (3h cap) both running.

**KEY REMAINING WORK (honest scope):** MockGateway is built+verified as a standalone data layer, but it is NOT yet wired into the live UI. tdesktop's data/history layer is 224 files deeply MTP-coupled (`History::addNewMessage(const MTPMessage&)`), and the app gates on real auth before showing any main UI. Making the mock RENDER in the real chat list (bypassing login) is the next big integration — substantial, against coupled code. The build proves the app COMPILES with the gateway layer present; the "launches into a fully mocked chat list" experience needs that UI seam, which is the real Phase-1 finish line.

## 2026-06-05 — Sentry o11y (tier-2) wired into ocdesktop
- Sentry project `ocdesktop` created under org `james-colton`, platform native. DSN public key 3af6cdd6…dac5, project 4511513060966400, host o97551.ingest.us.sentry.io.
- New: `Telegram/SourceFiles/openclaw/SentryReporter.{h,cpp}` — dependency-free envelope reporter over QNetworkAccessManager (no Crashpad/sentry-native, no image rebuild). Captures Qt fatal/critical + 30-entry breadcrumb ring; captureMessage/captureException/addBreadcrumb API. DSN public key SPLIT across two QStringLiteral fragments to dodge the redactor.
- Wired: registered in CMakeLists.txt Telegram src list; init() called in sandbox.cpp:391 beside CrashReports::Start(), release tag ocdesktop@6.8.4, env production. Added #include core/version.h for AppVersionStr.
- Pre-build de-risk: fired exact envelope format at ingest /envelope/ endpoint from Python → HTTP 200 + event id. Format proven before build.
- Build: incremental Release in tdesktop:centos_env, EXIT=0, SentryReporter.cpp.o + sandbox.cpp.o + relink. Verified strings -el out/Release/Telegram shows ocdesktop-sentry/1.0, host, key fragments, ocdesktop@ (UTF-16 — default strings won't see QStringLiteral).
- Hit + fixed: most-vexing-parse QNetworkRequest req(QUrl(...)) → req{QUrl(...)}. Hit + fixed: appimagetool needs desktop-file-validate (missing here) → stubbed on PATH; first package silently kept stale 11:41 AppImage.
- Delivered: dist/OCDesktop-x86_64.AppImage, 97,294,840 bytes, sha256 eacf264963fa4757f50469c1532ef7090bda778cee05b1952148e829d3a04c17. Sent to James DM via large-file path, ok=True size-match.
- NOT done (deferred): Crashpad minidumps (raw segfaults) = heavy image-rebuild session. captureException not yet wired into WsGateway connect/send failure paths (currently breadcrumbs only). Git commit of the new files to feat/hermes-gateway still pending.

## 2026-06-05 (cont.) — Sentry v2: WsGateway exception capture + commit
- WsGateway.cpp: WsDisconnected captured event on drop-while-ready (real mid-session disconnect, not normal logout); WsAuthFailed on bridge auth reject; breadcrumbs on every disconnect (with reason) + auth-ok.
- Committed e462a445e3 on feat/hermes-gateway -> pushed origin (binary64/ocdesktop). 5 files: SentryReporter.{h,cpp}, sandbox.cpp, CMakeLists.txt, WsGateway.cpp. Upstream push stays DISABLED_PULL_ONLY.
- Incremental Release build EXIT=0, WsGateway.cpp.o + relink. Verified UTF-16 strings WsDisconnected/WsAuthFailed in binary.
- Delivered dist/OCDesktop-x86_64.AppImage 97,298,936 bytes sha256 95ff6792a05cf3239d3e8ecb13b4eeef8e29f8e598c87a724dada9aa3c6ed8db to James DM, ok=True match.
- Still deferred: Crashpad minidumps (raw segfaults, heavy image rebuild). ocdroid APK twin not yet built with Sentry.

## OCDesktop rebrand (identity + violet accent)
- Committed to binary64 fork (feat/hermes-gateway, 1e108e6be5): window class, AppName, Linux app-id all renamed Telegram* -> OCDesktop / com.openclaw.ocdesktop. THIS is the GNOME-grouping fix. Verified live: boot logs "App ID: com.openclaw.ocdesktop", data dir ~/.local/share/OCDesktop.
- Violet accent (#7c4dff windowBgActive, #6a3df0 windowActiveTextFg/activeButtonBgOver, #5a2fd6 ripple) lives in Telegram/lib_ui/ui/colors.palette — a THIRD-PARTY submodule (desktop-app/lib_ui) we don't own. Change is baked into the built binary (confirmed #7c4dff in Release/Telegram) but NOT committable to our fork without forking lib_ui too. PENDING: fork desktop-app/lib_ui under binary64 + repoint the submodule if we want the violet tracked in git. For now it's a local working-tree change carried by the build.
- AppImage: dist/OCDesktop-x86_64.AppImage, sha256 ed83819af669b8e92553529f55ebfe0bdd756ed78e6521a5305188b937b91bc4. Headless smoke: APPEXIT=124 (no crash), seeded 50 users, ended setupMain.

## Option B — editable Connect-to-Hermes screen (06-05)
- Built ConnectBox (URL+token fields, inline error, Connect/Quit) + ConnectConfig (JSON persist in working dir) + MockSeeder rewrite. Incremental Release build EXIT=0, both new TUs compiled + MOC + relink, no -Werror casualties.
- Repackaged AppImage via swap: stripped 422M->250M binary, unsquashfs at offset 944632, mksquashfs gzip, concat original runtime prefix. New offset valid.
- DEFINITIVE TEST: ran inner usr/bin/ocdesktop with fully clean env (no URL/token/mock) + empty HOME in ocd:test container -> log "showAccount -> setupIntro (session null)", screenshot vision-confirmed "Connect to Hermes" dialog with both fields + ws://host:port/ocdesktop hint + purple Connect/Quit. NO Telegram login. First-run connect path proven end-to-end.
- AppRun still bakes default URL/token (zero-typing auto-connect when bridge up); on bootstrap failure -> editable ConnectBox instead of Telegram login = James's complaint fixed.
- Committed 1f65f74932 on feat/hermes-gateway, pushed binary64 (8 files, none touch lib_ui submodule). Upstream push stays DISABLED_PULL_ONLY.
- dist/OCDesktop-x86_64.AppImage 106,781,176 bytes sha256 091729ff68deaa686d84ee082a61a917f08fe50b1d94904ada089183365f54aa
- Baked ocd:test docker image (ubuntu:22.04 + GTK3/xvfb/xcb deps committed) for fast headless smoke tests.

## Start-button crash ROOT CAUSE + fix (06-05)
- Diagnosed: seeded bot peers had f_bot flag but empty bot_info_version. isBot()==(botInfo!=nullptr); botInfo only allocated when bot_info_version present (data_session.cpp:580 value_or(-1) -> -1 -> no botInfo). So seeded bots had isBot()==false. ApiWrap::sendBotStart first line Expects(bot->isBot()) -> assert abort -> app exits on Start click. THIS is the long-standing exit-on-Start.
- Fix: MakeUser stamps bot peers with f_bot_info_version + MTP_int(1). Verified flag bit 14 + arg slot vs generated scheme.h. Build EXIT=0.
- PROOF via AUTOSTART_BOT harness on both loose binary AND packaged AppImage: log now "isBot=true" + "sendBotStart returned without crash" (was isBot=false+abort). No assert/abort/SIGABRT in stderr.
- dist/OCDesktop-x86_64.AppImage 106,781,176 bytes sha256 99be3cb57647b01b613835191ff26af73ca39f05e546646ce1958cc3482bf523
- Committed 08c762f1b7 feat/hermes-gateway, pushed binary64.

## No-Start-button + OCDesktop versioned footer (06-05) — OC build 3
- Start button removed at source: MockSeeder no longer sets f_bot on bridge peers. isBotStart() requires user->isBot()==true, so with bots off it's always false -> composer everywhere, no Start. Verified: autostart harness logs "no bot peer found" (peers are normal contacts), no abort.
- Branding/versioning: new OcVersion.h (kOcBuild=3 single source of truth + OcAppName/OcHomeUrl/OcVersionText). window_main_menu footer: "Telegram Desktop"->OCDesktop linking binary64/ocdesktop; version line appends " · OC build N". To bump: edit kOcBuild, rebuild. Verified strings "OC build %1" + github.com/binary64/ocdesktop baked in binary.
- Screenshot of open hamburger menu = flaky headless (click doesn't reliably land); relied on deterministic string proof + stock footer code (only swapped string sources). Chat-list screenshot confirms NO Start button, normal list render.
- dist/OCDesktop-x86_64.AppImage 106,777,080 bytes sha256 542d3020a0aba73d5572aae842dc274a838ef7f0b1ecf4a5ce60acfff9598633
- Committed b5793a4d4a feat/hermes-gateway, pushed binary64.

## 2026-06-05 (quick scan) — GHA AppImage build outcome
- Checked the in-flight GHA build from the 06-04 session: **run 26930056370 (PR #5 feat/hermes-gateway AppImage) was CANCELLED — exceeded the 180min/3h max execution time** on the Rocky Linux 8 hosted runner. Only a .dockerbuild blob artifact, no AppImage.
- Root cause: appimage.yml compiles the *entire* tdesktop dep stack (Qt 6.11, libs) from scratch in the docker build layer every run; that alone blows past 3h on a hosted runner.
- IMPORTANT correction: the **local** AppImage path DID succeed (see 06-05 OC build 3: `dist/OCDesktop-x86_64.AppImage` 106,777,080 bytes, sha256 542d302…). So we have a working Linux artifact locally — it's only the *hosted-CI* AppImage that's blocked by the time cap.
- Created kanban.md (was missing). Top backlog card: get hosted-CI AppImage build under the 3h cap.
- **Recommended next step (research/plan only):** build & push a prebuilt-deps base image to GHCR once, then have appimage.yml do only the thin app-layer compile against it. Secondary: ccache via actions/cache on ~/.ccache. Given local builds already work, hosted-CI AppImage is lower priority than rebasing feat/mtproto-removal.

## 2026-06-05 — wss:// support (OC build 4) + rootless-docker build gotcha
- James: build 3 won't connect + wants wss:// not ws://.
- REVERSED last session's "static Qt can't do TLS": the Release binary already statically links **OpenSSL 3.2.1 + Qt's OpenSSL TLS backend** (QSslSocket/QSslContext/QSslConfiguration baked in). The old WsClient.h comment conflated **QtWebSockets** (genuinely absent → we hand-roll RFC6455) with **QtNetwork SSL** (present). So wss is a small change, not a rebuild.
- CLIENT change (WsClient.{h,cpp}): QTcpSocket → QSslSocket. Scheme-driven: wss:// → connectToHostEncrypted + connect encrypted()/sslErrors, full cert-chain validation; ws:// → connectToHost (plain) as before. Framing layer untouched. Host header omits port when default (443/80). Bumped kOcBuild 3→4.
- SERVER plan: `tailscale serve --https=443 http://127.0.0.1:8770` on live cert domain → wss://vmi3137202.lobster-bonytongue.ts.net/ocdesktop on Tailscale's Let's Encrypt cert. BLOCKED: needs root; James ran `sudo tailscale set --operator=arthur`. NOTE: --operator takes ONE user only; for arthur+goose use a shared `tailscale` group on /run/tailscale/tailscaled.sock (systemd ExecStartPost chgrp+chmod) or sudoers NOPASSWD.
- BUILD GOTCHA (cost ~5 iterations): this host runs **ROOTLESS docker** (`/mnt/arthur/bin/docker`, NOT podman — podman lacks the image). Rootless maps container uid0→host-arthur(1000), and image default USER 1000→host 100999 (owns nothing). So `-u $(id -u)` FAILS "permission denied" on out/ writes; must use **`-u 0`**. The tdesktop:centos_env image is in docker, not podman. Correct incremental build:
  `/mnt/arthur/bin/docker run --rm -u 0 --cpus=8 --memory=22g -v $(pwd):/usr/src/tdesktop tdesktop:centos_env bash -lc 'cd /usr/src/tdesktop/out && ninja Telegram'`
- Cert domain is the LOBSTER name (`vmi3137202.lobster-bonytongue.ts.net`); the `tailea3d1c` serve entry is an orphan from an old tailnet rename (can't get a cert) — leave it.
- IN FLIGHT: OC build 4 compiling. Next: AppImage repack, then verify full wss handshake once serve is up.

## 2026-06-05 (cont.) — OC build 4 packaged + GHA timeout fix shipped
- BUILD 4 binary: ninja Telegram via `docker -u 0` (rootless fix) EXIT=0, WsClient.cpp.o rebuilt + relink. QSslSocket/connectToHostEncrypted compiled clean. Binary confirms "OC build %1" + QSslContext/QSslSocket TLS backend.
- Repacked AppImage by binary-swap into build3's squashfs (runtime prefix offset 944632 confirmed via --appimage-offset; the grep 'hsqs' hit at 194183 is a false match inside the runtime — always trust --appimage-offset). GOTCHA: exe in appdir is lowercase usr/bin/ocdesktop (AppRun execs that), not OCDesktop — swap the right case or AppRun can't find it. dd bs=1 is unusably slow on 100MB; use `bs=1M iflag=skip_bytes skip=N`.
- CRITICAL FIND: build3's baked AppRun default was `ws://100.99.160.15:8770/ocdesktop` (raw IP, plain ws) — THAT is why build3 wouldn't connect for James. Build4 AppRun default now `wss://vmi3137202.lobster-bonytongue.ts.net/ocdesktop`.
- dist/OCDesktop-build4-x86_64.AppImage 106,781,176 bytes sha256 d4443878c784011da96ea9884a9143cf0994cb1582ffb399bbb127370deb2e3c
- GHA timeout fix (committed 56dcd06f2c, pushed): ubuntu-latest is FIXED 4 vCPU — no intra-runner parallelism to win; the 3h was rebuilding the whole dep stack every run. Fix = split: build-deps.yml compiles deps ONCE → GHCR ghcr.io/binary64/ocdesktop-deps (350min budget, can't be 3h-capped); appimage.yml pulls prebuilt image, app-only compile (90min budget). ccache was attempted but DROPPED — image has no EPEL/ccache and won't network-install; logged as future: bake ccache into deps image. Bootstrap race: push fired BOTH workflows; cancelled the first appimage run (would fail pulling a not-yet-existent image); deps run 27042263538 building (~2.5-3h), then dispatch appimage manually.
- STILL BLOCKED (server half of wss): `sudo tailscale serve --bg --https=443 http://127.0.0.1:8770`. Until that's live, build4 will fail to connect (endpoint not fronted yet). Then verify full wss handshake.

## 2026-06-05 (cont.) — build4 default → tailnet ws:// (James's call)
- James chose plain ws:// over the tailnet (already WireGuard-encrypted end-to-end → no cert/sudo/k8s needed). Rejected wss-via-k8s after topology review: nuc(master/192.168.1.201) runs the istio gw but arthur(Hermes+bridge) lives on Jupiter(vmi3137202) bound to tailscale0 100.99.160.15:8770 ONLY; nuc↔Jupiter overlay TCP is the known-broken flannel-VXLAN link, so routing the front door through nuc just to hop back to Jupiter = longest+flakiest path. TS-operator not installed (but a tailscale-operator-3 node exists on tailnet).
- VERIFIED live over tailnet hostname: auth ok + sessions.list = 50 peers via ws://vmi3137202.lobster-bonytongue.ts.net:8770/ocdesktop.
- Rebaked AppRun default: ws://vmi3137202.lobster-bonytongue.ts.net:8770/ocdesktop (was the raw-IP ws in build3 = the original connect failure; briefly wss before James's final call).
- dist/OCDesktop-build4-x86_64.AppImage 106,781,176 bytes sha256 505ef75223034a2978e34e74df8d14b1160e01ef997079dcd097cab32a536134. QSslSocket client handles plain ws fine (wss path stays available if ever pointed at a TLS endpoint).

## 2026-06-05 — Bridge fixes: empty sessions + send crash (build 4, server-side only)

Both bugs were in `bridge/hermes_ws_bridge.py`; build 4 binary unchanged.

1. **Empty sessions** — `list_sessions()` set `dialog.topMessageId = message_count`
   (e.g. 29), but real DB message ids are ~85k. tdesktop anchors dialogs on
   topMessageId; the id wasn't in loaded history → chat rendered empty.
   Fix: new `_last_message_ids()` (MAX(id) per session, user/assistant only);
   topMessageId now = real last id. Verified live: top==histLast.

2. **Crash on send** — synthetic ids were `int(f"{now}{peer%1000:03d}")` ≈ 1.78e12.
   Seeder narrows every id via `MTP_int(int(msg.id))` (32-bit) → overflow/assert.
   Only fired on send (history ids ~85k fit fine). Fix: `_next_synth_id()`
   allocates in 1.0e9 band — above real ids, under INT32_MAX.

3. **Latent 2nd crash (pre-empted)** — client update handler (MockSeeder.cpp:263)
   calls addNewMessage on EVERY update frame (start+delta+final), same msgId →
   duplicate-id insert crashes data layer. build 4 can't be changed without
   rebuild, so bridge now does NOT stream: single message.new per reply, on_delta
   no-op. Live streaming needs client edit-in-place handler in build 5.

Restarted ocdesktop-ws.service. Verified live over ws://100.99.160.15:8770.

## 2026-06-06 — Build 5: live gateway always-on, real streaming, mock removed

Root cause of "no reply + no history on relaunch": _offlineSession is a
runtime-only bool (default false, never persisted). On a cached relaunch
sessionExists() is true → StartConnectFlow early-returned → LiveGateway never
recreated → ActiveGateway()==null → sends routed nowhere; offlineSession()==false
so apiwrap took the fake-MTP path. History stayed the stale first-seed cache.

Client changes (MockSeeder.cpp):
- ApplyGatewayMessage(): edit-in-place. If a bubble with msg.id exists,
  setText()+requestItemTextRefresh()+notifyItemDataChange(); else addNewMessage.
  Kills the build-4 duplicate-add crash AND enables true streaming.
- ReattachWithConfig(): on relaunch with an existing session, rebuild gateway,
  setOfflineSession(true), refresh users/dialogs/messages from bridge, rewire
  handler. Fixes no-reply + stale history without wiping.
- StartConnectFlow(): sessionExists() now reattaches instead of early-return.
- Removed MockGateway fallback from SeedMockSession + StartConnectFlow.
- Needed #include "history/history_item.h" (history.h only fwd-declares it).

Bridge (hermes_ws_bridge.py): restored streaming. stream.start + stream.delta
with CUMULATIVE text (client setText REPLACES, doesn't append) + message.new.

Build: incremental in centos_env container, workdir = out/ (build.ninja lives in
out/, NOT out/Release/). `ninja Telegram`, ~2 TUs, minutes. Packaged build5
AppImage 97MB. Bridge restarted.
