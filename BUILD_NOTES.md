
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
