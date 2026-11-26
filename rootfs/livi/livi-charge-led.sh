#!/bin/sh
# Own only the front indicator; never touch either camera flash LED.
sysfs=${BEGONIA_CHARGE_SYSFS:-/sys}
led=$sysfs/class/leds/white:indicator
[ -w "$led/brightness" ] || exit 1
printf 'none\n' > "$led/trigger"
cleanup() { printf '0\n' > "$led/brightness"; }
trap 'exit 0' TERM INT
trap cleanup EXIT
last=
while :; do
    value=0
    for supply in "$sysfs"/class/power_supply/mt6360-chg*; do
        read -r online < "$supply/online" || online=0
        read -r status < "$supply/status" || status=Unknown
        if [ "$online" = 1 ]; then
            case "$status" in Charging|Full) value=1 ;; esac
        fi
    done
    if [ "$value" != "$last" ]; then
        printf '%s\n' "$value" > "$led/brightness" || exit 1
        last=$value
    fi
    sleep 2 & wait $!
done
