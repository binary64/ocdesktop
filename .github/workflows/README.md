## CI hygiene: fork-live vs. upstream-only workflows

This repo is a personal fork of `telegramdesktop/tdesktop`. Fork-live workflows are
`appimage.yml` and `build-deps.yml` (plus `linux.yml`, `mac.yml`, `mac_packaged.yml`,
`win.yml`, `winget.yml`, `docker.yml`, `snap.yml`, `stale.yml`, `changelog.yml`,
`issue_closer.yml`, `master_updater.yml`, `copyright_year_updater.yml`) — these still
run and matter here. Upstream-only issue-triage/bot workflows inherited from
upstream (`cant-reproduce.yml`, `needs-user-action.yml`, `waiting-for-answer.yml`,
`lock.yml`, `unused_styles_updater.yml`, `user_agent_updater.yml`) cannot function on
a fork (no labels, no bot permissions) and are disabled by renaming to `*.disabled`,
following the existing `snap.yml.disabled`-style convention. Rule: any future upstream
sync that re-introduces one of these bot workflow files must re-disable it the same
way (rename to `*.disabled`) rather than leaving it live.
