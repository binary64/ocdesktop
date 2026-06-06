# ocdesktop — Kanban

## Doing
- (none active)

## Backlog
- **CI: AppImage build exceeds 3h cap on hosted runner** — GHA run 26930056370 cancelled at 3h0m. Full tdesktop compile from scratch in the centos/Rocky-8 docker layer doesn't fit. Options: (a) split build into prebuilt-deps base image cached in GHCR + thin app-layer build, (b) enable ccache across runs (actions/cache for ~/.ccache), (c) Release→Debug for the artifact build to cut compile time, (d) fix self-hosted runner (2h cap, more cores). Most leverage: prebuilt deps base image — Qt/libs compile is the bulk of the 3h.
- Rebase `feat/mtproto-removal` onto v6.8.4/dev `main` (real protocol-layer conflicts expected).
- Decide CI strategy: self-hosted GHA runner (containerd socket fix) vs GitHub-hosted only.
- Confirm/produce a working Linux AppImage artifact; document the build command. ✅ local build works (OC build 3, dist/OCDesktop-x86_64.AppImage 106MB) — only hosted-CI AppImage remains blocked (see top card).

## Done
- Upstream catch-up v6.6.4 → v6.8.4, then onto dev; main is clean mirror of upstream/dev (2026-06-04).
- PR #5 `feat/hermes-gateway`: GatewayInterface + MockGateway + mock.json fixtures, compiled & verified in Qt6 container (2026-06-04).
- Disabled stock fork workflows; appimage.yml added.
