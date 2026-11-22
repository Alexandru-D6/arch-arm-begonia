#!/bin/bash
# Record five seconds from the handset mic, then play the recording over AUX.
set -euo pipefail
usage() {
    cat <<'EOF'
Usage: begonia-mic-aux-test [--alsa | --pipewire]

Connect a three-pole AUX cable and select AUX on the car stereo first.
  --alsa      Test the hardware and UCM directly (default). Stop this user's
              WirePlumber/PipeWire services and sockets first if they own ALSA.
  --pipewire  Test through the logged-in audio user's PipeWire session.

Records exactly 240000 stereo frames (5 seconds at 48 kHz). The temporary
recording is deleted on exit. Speak after the recording prompt.
EOF
}
backend=alsa
case ${1:---alsa} in
    --alsa) ;;
    --pipewire) backend=pipewire ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then usage >&2; exit 2; fi
require() {
    command -v "$1" >/dev/null || { echo "Missing command: $1" >&2; exit 1; }
}
require mktemp
require python3
if [[ $backend == alsa ]]; then
    for cmd in alsactl alsaucm amixer arecord aplay; do require "$cmd"; done
else
    for cmd in pw-dump pw-record pw-play timeout; do require "$cmd"; done
fi
umask 077
work=$(mktemp -d "${TMPDIR:-/tmp}/begonia-mic-aux.XXXXXXXX")
restore=false
cleanup() {
    status=$?
    trap - EXIT
    if $restore; then
        if ! alsactl -f "$work/mixer.state" restore begoniaaux; then
            echo "Could not restore the previous mixer settings." >&2
            status=1
        fi
    fi
    rm -rf -- "$work"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [[ $backend == alsa ]]; then
    if ! amixer -c begoniaaux info >/dev/null; then
        echo "No begoniaaux sound card. Install the matching audio kernel and rootfs." >&2
        exit 1
    fi
    if ! amixer -c begoniaaux cget "iface=CARD,name='AUX Jack'" | grep -q 'values=on'; then
        echo "AUX is not detected. Plug in the cable and check the MT6359 jack detector." >&2
        exit 1
    fi
    alsactl -f "$work/mixer.state" store begoniaaux
    restore=true
    alsaucm -c hw:begoniaaux set _verb HiFi set _enadev Headphones set _enadev Mic1
    echo "Recording the internal microphone for 5 seconds. Speak now..."
    arecord -D hw:CARD=begoniaaux,DEV=1 -t wav -f S16_LE -r 48000 -c 2 -d 5 "$work/mic.wav"
else
    # Refuse to fall back to an unrelated default source or speaker.
    # Socket activation may start WirePlumber on this first client connection.
    nodes_ready=false
    for attempt in {1..20}; do
        if pw-dump | python3 -c '
import json, sys
nodes = {o.get("info", {}).get("props", {}).get("node.name"): o
         for o in json.load(sys.stdin) if o.get("type") == "PipeWire:Interface:Node"}
for name, kind in (("begonia_internal_mic", "Audio/Source"), ("begonia_aux_output", "Audio/Sink")):
    props = nodes.get(name, {}).get("info", {}).get("props", {})
    if props.get("media.class") != kind or props.get("api.alsa.card.name") != "begonia-aux":
        sys.exit(1)
'; then
            nodes_ready=true
            break
        fi
        sleep 0.25
    done
    if ! $nodes_ready; then
        echo "Missing onboard audio nodes. Check WirePlumber and UCM." >&2
        exit 1
    fi
    echo "Recording the internal microphone for 5 seconds. Speak now..."
    record_status=0
    timeout --signal=INT --kill-after=2s 15s pw-record \
        --target=begonia_internal_mic --rate=48000 --channels=2 --format=s16 \
        --sample-count=240000 "$work/mic.wav" || record_status=$?
    # Some pw-cat versions return 1 on reaching --sample-count. Accept that
    # status only when the complete five-second WAV passes validation below.
    if (( record_status != 0 && record_status != 1 )); then
        echo "Microphone recording failed (status $record_status)." >&2
        exit "$record_status"
    fi
fi
python3 - "$work/mic.wav" <<'PY'
import sys, wave
with wave.open(sys.argv[1], "rb") as recording:
    actual = (recording.getframerate(), recording.getnchannels(),
              recording.getsampwidth(), recording.getnframes())
    if actual != (48000, 2, 2, 240000):
        sys.exit("Recording is incomplete or has the wrong format: " + str(actual))
    if len(recording.readframes(240000)) != 960000:
        sys.exit("Recording contains truncated PCM data")
PY
echo "Playing the recording through AUX..."
if [[ $backend == alsa ]]; then
    aplay -D hw:CARD=begoniaaux,DEV=0 "$work/mic.wav"
else
    timeout --signal=INT --kill-after=2s 15s pw-play \
        --target=begonia_aux_output "$work/mic.wav"
fi
echo "Finished. Confirm that your voice was audible through both AUX channels."
