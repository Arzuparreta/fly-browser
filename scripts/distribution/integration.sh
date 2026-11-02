#!/bin/sh
# Native package hooks. Never starts Fly or changes a user's profile/defaults.
set -eu
source=/usr/share/fly-browser/apparmor
profile=/etc/apparmor.d/fly-browser
case ${1:-} in
  install)
    # Older AppArmor parsers cannot read the userns rule. Only install it
    # where the corresponding ABI is available; SUID sandbox remains usable.
    if [ -f /etc/apparmor.d/abi/4.0 ]; then
      install -m 644 "$source" "$profile"
      if [ -d /sys/kernel/security/apparmor ]; then
        apparmor_parser -r "$profile"
      fi
    fi ;;
  remove)
    if [ -f /etc/pacman.d/fly-browser.conf ] && [ -f /etc/pacman.conf ]; then
      # The bootstrap's include must not outlive its package-owned file.
      sed -i '\|^Include = /etc/pacman.d/fly-browser.conf$|d' /etc/pacman.conf
    fi
    if [ -f "$profile" ] && cmp -s "$source" "$profile"; then
      if [ -d /sys/kernel/security/apparmor ]; then
        apparmor_parser -R "$profile" || true
      fi
      rm -f "$profile"
    fi ;;
  *) exit 2 ;;
esac
if command -v update-desktop-database >/dev/null; then
  update-desktop-database /usr/share/applications || true
fi
