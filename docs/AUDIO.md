# Begonia AUX and internal microphone

Workflow 02 builds an experimental 48 kHz, signed 16-bit stereo audio path for
step 4 of the Android Auto plan. Install its matching boot and rootfs images.
Workflow 04 alone does not install the audio modules or userspace configuration.
Five-second internal-mic recording and stereo AUX playback were verified on the
attached begonia on 2026-11-22, through ALSA and PipeWire, including AUX replugging
and another boot with AUX inserted. The user confirmed clear voice on both
channels. See [the hardware test report](AUDIO_TEST_RESULTS.md) for scope and limits.

The ALSA card is `begoniaaux`, with card/driver name `begonia-aux`:

| Direction | ALSA PCM | Path |
| --- | --- | --- |
| Playback | `hw:CARD=begoniaaux,DEV=0` | DL1 → ADDA/MTKAIF → MT6359 → left/right headphone pins → car AUX |
| Capture | `hw:CARD=begoniaaux,DEV=1` | AIN0/MICBIAS0 → ADC L → MTKAIF → VUL12 |

The initial handset microphone configuration uses AIN0, 1.9 V MICBIAS0, DCC
coupling and 12 dB analog gain. Its mono signal is duplicated into the two DMA
channels. This configuration produced the confirmed voice recording on the
tested handset; other board variants still require verification.
The existing codec driver supplies bias and headphone power sequencing.
MTKAIF calibration must succeed before the card registers; it never silently
falls back to an uncalibrated capture link. The two active MTKAIF lanes calibrate
at phase 3, cycle 3 on the tested handset. The unused third lane is excluded.

The MT6359 accessory detector supplies `AUX Jack`. Only AUX availability
follows that jack; the internal mic has no jack dependency. UCM exposes one
capture device and no headset microphone or speaker path. WirePlumber 0.5+
selects HiFi and gives the onboard source/sink high default priorities with
stable names `begonia_internal_mic` and `begonia_aux_output`. Existing explicit
user selections can override default priorities.

The AFE power domain stays enabled during bring-up because its uncached register
window is shared with the clock controller. DAPM gates the converter and codec
paths. Runtime power saving, suspend/resume and speaker amplification are not
qualified by this change.

## Five-second test

Plug a three-pole 3.5 mm cable into the Redmi and the car AUX input, select AUX
on the car stereo and start with a low car volume. Run as `alarm` (or the future
kiosk account), after logging in again if audio group membership changed.
Workflow 02 enables that user's audio services through systemd user defaults
and lingering, so the PipeWire test also works over SSH.

```bash
aplay -l
arecord -l
systemctl --user status pipewire wireplumber
wpctl status -n
begonia-mic-aux-test --pipewire
```

The command records exactly 240000 frames from the onboard internal microphone,
checks the five-second WAV file, then plays it through the AUX node. Speak after
the prompt. It targets the devices explicitly, reports missing nodes instead of
using another sound card, and deletes the private temporary recording on exit.

For a direct hardware/UCM test, release the ALSA PCMs first:

```bash
systemctl --user stop wireplumber.service pipewire-pulse.service pipewire-pulse.socket pipewire.service pipewire.socket
begonia-mic-aux-test --alsa
systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber.service
```

The ALSA test also requires jack detection. It saves/restores the card's previous
mixer state, including if capture fails, and does not proceed to playback after
a failed or truncated recording. The source script is
[`scripts/test-mic-aux.sh`](../scripts/test-mic-aux.sh).

## Updating an existing rootfs

These commands run on the handset from a checkout of this repository. The
handset must already be booting the matching rebuilt kernel with its modules.

```bash
sudo pacman -S --needed alsa-utils alsa-ucm-conf pipewire pipewire-audio pipewire-alsa pipewire-pulse wireplumber python
sudo bash rootfs/install-audio.sh / alarm
sudo reboot
```

For another kiosk user, pass its account name in place of `alarm`. The account
must exist before running the installer. The helper copies configuration and the
test command, grants audio access and enables user audio services; it does not
install a kernel or packages itself.

## Hardware acceptance checklist

1. Cold boot with AUX already inserted and confirm `begoniaaux`, playback PCM 0
   and capture PCM 1 enumerate. Check `dmesg` for the two MTKAIF calibration
   phases and cycles. Run both test modes and listen for clear speech.
2. Verify left/right routing with `speaker-test -D plughw:CARD=begoniaaux,DEV=0
   -r 48000 -c 2 -t wav` after enabling the HiFi Headphones device, while another
   process records the handset mic. Keep both UCM devices enabled for this test.
3. Replug AUX and repeat both tests, confirming the capture source stays the
   internal mic. Repeat after reboot, including booting with AUX disconnected.
4. Repeat with car USB charging, check for charging noise, and check far-end echo
   at normal car volume once LIVI voice is available. The record/playback helper
   does not test simultaneous duplex, echo cancellation or Bluetooth SCO.

Useful diagnostics:

```bash
cat /proc/asound/cards
amixer -c begoniaaux contents
alsaucm -c hw:begoniaaux list _verbs
amixer -c begoniaaux cget "iface=CARD,name='AUX Jack'"
journalctl --user -b -u wireplumber -u pipewire
sudo dmesg | rg -i 'mt6785|mt6359|mtkaif|asoc|audio'
```

## Implementation and host validation

`boot/configure-kernel.sh`, used by workflows 02 and 04, applies
`boot/kernel-patches/series` and the files in `boot/kernel-overlay`. Applying it
again to a cached kernel is supported. It enables the machine driver, codec and
accessory detector as modules and checks that Kconfig retained them. Workflow 02
checks the installed audio modules and invokes `rootfs/install-audio.sh`.

Host validation:

```bash
python3 -m unittest discover -s tests -v
```

The tests cover explicit PCM/node selection, capture-before-playback, recording
length, missing hardware, failure cleanup, rootfs installation and ALSA syntax.
The kernel, modules and begonia DTB have also been cross-compiled against the
selected pmOS 6.16.4-r0 tree. Host tests do not establish electrical routing or
sound quality.

References: Xiaomi's [MT6785 audio driver](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/begonia-p-oss/sound/soc/mediatek/mt6785)
provides the DMA, interrupt, ADDA and calibration programming;
[MT6785 pins and IRQ](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/arch/arm64/boot/dts/mediatek/mt6785.dts)
and [begonia jack settings](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/arch/arm64/boot/dts/mediatek/xiaomi-mt6785-common.dtsi)
provide the board baseline. Userspace follows the
[ALSA UCM format](https://www.alsa-project.org/alsa-doc/alsa-lib/group__ucm__conf.html)
and [WirePlumber ALSA rules](https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/alsa.html).
