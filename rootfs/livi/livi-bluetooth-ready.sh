#!/bin/sh
# Let PipeWire publish Hands-Free before LIVI's client-only profile claims it.
# Keep the UI available if Bluetooth is absent or still starting.
if ! systemctl --user start wireplumber.service; then
    echo 'LIVI: WirePlumber could not start; Bluetooth projection is unavailable.' >&2
    exit 0
fi
attempt=0
while [ "$attempt" -lt 50 ]; do
    if busctl --system --timeout=1 get-property org.bluez /org/bluez/hci0 \
        org.bluez.Adapter1 UUIDs 2>/dev/null |
        grep -qi '0000111e-0000-1000-8000-00805f9b34fb'; then
        exit 0
    fi
    attempt=$((attempt + 1))
    sleep 0.2
done
echo 'LIVI: Hands-Free advertisement not ready; starting the UI for diagnosis.' >&2
