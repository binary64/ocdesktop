#!/bin/bash
# Incremental ocdesktop build via the centos_env docker image.
# Mounts repo at /usr/src/tdesktop (cache key), runs as root (rootless userns),
# builds the Telegram target into out/.
set -uo pipefail
REPO=/mnt/arthur/clawd/projects/ocdesktop
docker run --rm -u 0 \
  -v "$REPO":/usr/src/tdesktop \
  -w /usr/src/tdesktop \
  tdesktop:centos_env \
  bash -lc 'cmake --build out --target Telegram 2>&1'
echo "EXIT=$?"
