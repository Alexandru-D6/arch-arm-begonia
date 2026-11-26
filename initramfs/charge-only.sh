#!/bin/busybox ash
# Run after pmOS setup_udev, before networking or mounting the Arch rootfs.
# MTK atag,boot contains native little-endian u32s, including bootmode at +8.
sysfs=${BEGONIA_CHARGE_SYSFS:-/sys}
cmdline=${BEGONIA_CHARGE_CMDLINE:-/proc/cmdline}
kmsg=${BEGONIA_CHARGE_KMSG:-/dev/kmsg}

log() {
    printf '<6>[begonia-charge-only] %s\n' "$*" >> "$kmsg"
}

charging_boot() {
    case " $(cat "$cmdline") " in
        *' begonia.charge_only=0 '*) return 1 ;;
    esac
    tr '\000' '\n' < "$sysfs/firmware/devicetree/base/compatible" 2>/dev/null |
        grep -Fxq 'xiaomi,begonia' || return 1
    # Validate the vendor tag, not just the bootreason string. A normal
    # power-button start or intentional reboot must always start Arch.
    set -- $(od -An -tu4 -N12 "$sysfs/firmware/devicetree/base/chosen/atag,boot" 2>/dev/null)
    [ "$#" = 3 ] && [ "$1" = 3 ] && [ "$2" = 1090521090 ] || return 1
    bootmode=$3
    case "$bootmode" in 8|9) return 0 ;; *) return 1 ;; esac
}

charging_boot || exit 0
[ "${1:-}" != --check ] || exit 10

# A broken or incomplete initramfs must not strand the user in this mode.
command -v iskey >/dev/null 2>&1 || { log 'iskey unavailable; continuing boot'; exit 0; }
keys_found=false
for name in "$sysfs"/class/input/event*/device/name; do
    [ -r "$name" ] || continue
    [ "$(cat "$name")" != mtk-pmic-keys ] || keys_found=true
done
$keys_found || { log 'PMIC power key unavailable; continuing boot'; exit 0; }

charger=
for path in "$sysfs"/class/power_supply/mt6360-chg*; do
    [ -r "$path/online" ] && charger=$path && break
done
[ -n "$charger" ] || { log 'charger unavailable; continuing boot'; exit 0; }

backlight="$sysfs/class/backlight/lm36274-backlight"
brightness=
bl_power=
if [ -r "$backlight/brightness" ]; then
    read -r brightness < "$backlight/brightness"
    read -r bl_power < "$backlight/bl_power"
    printf '0\n' > "$backlight/brightness"
    printf '4\n' > "$backlight/bl_power"
fi
hide_display() {
    if [ -n "$brightness" ]; then
        printf '0\n' > "$backlight/brightness"
        printf '4\n' > "$backlight/bl_power"
    fi
}
show_display() {
    capacity=-1
    mv=0
    for supply in "$sysfs"/class/power_supply/*; do
        [ -r "$supply/type" ] || continue
        [ "$(cat "$supply/type")" = Battery ] || continue
        read -r capacity < "$supply/capacity" || capacity=-1
        case "$capacity" in *[!0-9]*|'') capacity=-1 ;; esac
        [ "$capacity" -le 100 ] || capacity=-1
        break
    done
    # MT6360 voltage ADC scale is in microvolts. Find by label, not IIO index.
    for label in "$sysfs"/bus/iio/devices/iio:device*/in_voltage*_label; do
        [ -r "$label" ] || continue
        [ "$(cat "$label")" = vbat ] || continue
        base=${label%_label}
        mv=$(awk 'FNR==1 { a[++n]=$1 } END { if(n==3) printf "%d", (a[1]+a[3])*a[2]/1000 }' "${base}_raw" "${base}_scale" "${base}_offset" 2>/dev/null)
        case "$mv" in *[!0-9]*|'') mv=0 ;; esac
        [ "$mv" -ge 2500 ] && [ "$mv" -le 4600 ] || mv=0
        break
    done
    read -r state < "$charger/status" || state=Unknown
    active=0
    [ "$state" != Charging ] || active=1
    if "$display" "$capacity" "$mv" "$active"; then
        if [ -n "$brightness" ]; then
            read -r maximum < "$backlight/max_brightness" || maximum=$brightness
            printf '0\n' > "$backlight/bl_power"
            printf '%s\n' "$((maximum / 10 + 1))" > "$backlight/brightness"
        fi
        visible=60
    else
        log 'charging display unavailable; keeping screen dark'
    fi
}
restore_backlight() {
    [ ! -w "$led/brightness" ] || printf '0\n' > "$led/brightness"
    hide_display
    [ ! -x "$display" ] || "$display" --restore

    if [ -n "$brightness" ]; then
        printf '%s\n' "$bl_power" > "$backlight/bl_power"
        printf '%s\n' "$brightness" > "$backlight/brightness"
    fi
}
led="$sysfs/class/leds/white:indicator"
led_ready=false
last_led=
trap restore_backlight EXIT

log "entered charging-only mode (boot mode $bootmode); hold Power to start Arch"
modprobe -q mt6360-adc 2>/dev/null || true
modprobe -q leds-mt6360 2>/dev/null || true
display=${BEGONIA_CHARGE_DISPLAY:-/usr/bin/begonia-charge-display}
visible=0
held=0
offline=0
ticks=0
seen_online=false
last_online=
while :; do
    if iskey KEY_POWER; then
        # Show on the press edge, without waiting for release. A long hold
        # still follows the existing vibration/release path to start Arch.
        [ "$held" -ne 0 ] || show_display
        held=$((held + 1))
    else
        result=$?
        [ "$result" = 1 ] || { log 'power-key check failed; continuing boot'; exit 0; }
        held=0
    fi
    if [ "$visible" -gt 0 ]; then
        visible=$((visible - 1))
        [ "$visible" -ne 0 ] || hide_display
    fi
    if [ "$held" -ge 20 ]; then
        log 'Power held; waiting for release before starting Arch'
        # The bootloader is not re-entered on this path, so supply the
        # recognition pulse here using the existing pmOS feedback utility.
        modprobe -q regulator-haptic 2>/dev/null || true
        if beebzzr -s 40000 -d 120; then
            log 'Power recognition vibration sent'
        else
            log 'Power recognition vibration unavailable; continuing'
        fi
        while iskey KEY_POWER; do sleep 0.05; done
        log 'Power released; starting Arch'
        exit 0
    fi

    read -r online < "$charger/online" || online=
    case "$online" in
        0)
            # MT6360 can initially report offline while Type-C settles.
            # Powering down at that point creates a USB boot/poweroff loop.
            $seen_online && offline=$((offline + 1))
            ;;
        1) seen_online=true; offline=0 ;;
        *) log 'charger state unreadable; continuing boot'; exit 0 ;;
    esac
    if [ "$offline" -ge 40 ] && [ "$held" = 0 ]; then
        log 'USB removed; powering off'
        poweroff -f
        log 'poweroff returned unexpectedly; continuing boot'
        exit 0
    fi
    # The rootfs/LIVI service is deliberately absent in this mode. Own the
    # same front indicator here, after its early driver has been loaded.
    if [ -w "$led/brightness" ]; then
        if ! $led_ready; then
            printf 'none\n' > "$led/trigger"
            led_ready=true
        fi
        read -r status < "$charger/status" || status=Unknown
        value=0
        if [ "$online" = 1 ]; then
            case "$status" in Charging|Full) value=1 ;; esac
        fi
        if [ "$value" != "$last_led" ]; then
            printf '%s\n' "$value" > "$led/brightness"
            last_led=$value
        fi
    fi
    if [ "$ticks" = 0 ] || [ "$online" != "$last_online" ]; then
        read -r status < "$charger/status" || status=Unknown
        log "charger status=$status, online=$online"
    fi
    last_online=$online
    ticks=$(((ticks + 1) % 1200))
    sleep 0.05
done
