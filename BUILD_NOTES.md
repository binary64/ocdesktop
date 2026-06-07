
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
