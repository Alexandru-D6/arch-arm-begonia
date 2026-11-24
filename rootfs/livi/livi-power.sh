#!/bin/bash
# Root-owned endpoint used by LIVI's power menu.
set -euo pipefail
case "${1:-}" in
    poweroff|reboot) exec /usr/bin/systemctl "$1" ;;
    *) echo 'Usage: livi-power.sh poweroff|reboot' >&2; exit 2 ;;
esac
