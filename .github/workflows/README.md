## CI hygiene: fork-live vs. legacy vs. upstream-only workflows

This repo is a personal fork of `telegramdesktop/tdesktop`. The workflow files here fall
into three categories. Enablement state below matches what GitHub reports for
`binary64/ocdesktop` (`gh workflow list --all -R binary64/ocdesktop`).

### 1. Active fork workflows

These are enabled and matter here:

- `appimage.yml` — AppImage (Hermes mock build).
- `build-deps.yml` — Build deps image (centos_env -> GHCR).
- `changelog.yml` — Changelog
- `copyright_year_updater.yml` — Copyright year updater.
- `issue_closer.yml` — Issue closer.
- `master_updater.yml` — Master branch updater.
- `stale.yml` — Close stale issues and PRs

GitHub also lists the built-in `Dependabot Updates` workflow, which has no file in this
directory.

### 2. Legacy platform/release workflows (present but manually disabled)

These files are still checked in unchanged, but are disabled at the GitHub level
(`disabled_manually`), so they do not run:

- `docker.yml` — Docker.
- `linux.yml` — Linux.
- `mac.yml` — MacOS.
- `mac_packaged.yml` — MacOS Packaged.
- `snap.yml` — Snap.
- `win.yml` — Windows.
- `winget.yml` — Publish to WinGet

Do not assume any of these runs. Re-enabling one is a deliberate decision, not a side
effect of an upstream sync.

### 3. Upstream-only issue-triage workflows (disabled durably by rename)

Issue-triage/bot workflows inherited from upstream cannot function on a fork (no labels,
no bot permissions), so they are disabled durably in git by renaming to `*.disabled`:

- `cant-reproduce.yml.disabled`
- `lock.yml.disabled`
- `needs-user-action.yml.disabled`
- `unused_styles_updater.yml.disabled`
- `user_agent_updater.yml.disabled`
- `waiting-for-answer.yml.disabled`

Rule: any future upstream sync that re-introduces one of these bot workflow files must
re-disable it the same way (rename to `*.disabled`) rather than leaving it live.
