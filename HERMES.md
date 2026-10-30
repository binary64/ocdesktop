# ocdesktop

> Project charter. Seeded 2026-05-29; populated from memory 2026-05-29.

## What
The household's fork of Telegram Desktop (tdesktop) — a native **C++/Qt** desktop client for
the OpenClaw Gateway. Repo: [`binary64/ocdesktop`](https://github.com/binary64/ocdesktop)
(public since 2026-03-24, for unlimited GHA minutes). NOT the old Electron app
(`clawd-telegram-electron`, archived) and NOT `desktop-portal` (the web portal that
superseded the original "ocdesktop" desktop-portal naming).

## Why
A first-class, performant desktop client built on tdesktop's mature Qt UI framework —
reusing Telegram's battle-tested chat UI rather than rebuilding it in Electron. Lower
memory footprint, native feel, and full control over the client surface for OpenClaw.

## Status (as of 2026-05-29)
- Repo public; CI exists but historically fragile.
- **PR #1** `fix/linux-cmake-qt-private` — Linux CMake / Qt private headers build fix.
- **CI blocker (known):** self-hosted GHA runner on Jupiter fails — `/var/run/docker.sock
  is not a socket` (RKE2/containerd, no Docker daemon). Pre-existing since ~Mar 29.
  Kaniko builds also hit pkg-config / long (3.5h+) C++ compile issues.
- Builds work better on GitHub-hosted runners; self-hosted path is the pain point.

## Notes (seeded)
- Build is heavy: full tdesktop library compilation (Qt, lib_spellcheck, lib_storage, etc).
- Decision point: is self-hosted CI worth fixing, or stay on GitHub-hosted runners?
- See `clawd/projects/coding/research/2026-03-30-gha-runner-docker-socket-failure.md`.
