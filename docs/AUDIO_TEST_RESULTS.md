# Handset audio and peripheral tests

## Internal speaker fallback — 2026-11-23

The same Redmi Note 8 Pro now exposes AUX playback on PCM 0, internal microphone
capture on PCM 1, and mono loudspeaker playback on PCM 2. The matching speaker
boot image, modules, UCM and WirePlumber profiles were installed without replacing
userdata. The ALSA card ID remains `begoniaaux` for compatibility.

| Check | Result | Evidence and scope |
| --- | --- | --- |
| Amplifier identification | Pass | TFA9874 revision `0x0c74`, using the handset's stock register fields. GPIO I2C on GPIO45/46 works; the hardware I2C6 path returned `ENXIO`. Reset on GPIO24 is active-high. |
| Internal speaker | Pass | Three-second 700 Hz tone played through `begonia_speaker_output`, status 0. User confirmed clear sound from the phone. |
| Stereo downmix after reboot | Pass | Default-routed left-only 700 Hz and right-only 1000 Hz tones both appeared in the internal mic recording. Playback completed with status 0; capture contained exactly 240000 frames. |
| Speaker + mic duplex | Pass, basic | Five-second 240000-frame capture while the tone played. Measured 700 Hz amplitude rose from 9 before playback to approximately 10704 during playback. This confirms acoustic output and concurrent capture, not echo cancellation. |
| Automatic AUX selection | Pass | Connecting AUX changed the default sink to `begonia_aux_output`; the mic remained `begonia_internal_mic`. |
| Active stream switching | Pass, basic | PipeWire links moved between stereo AUX and mono speaker as the cable was toggled. User confirmed playback moved to the speaker after unplugging. A transient link activation warning occurred during the toggle/stop test; subsequent playback completed with status 0. |
| Idle amplifier | Pass | After the five-second sink suspend timeout, DAPM reports `Speaker Amplifier: Off`; system register `0x00` is `0x0001` (power-down, output and boost off). |
| Boot without AUX | Pass | Speaker codec and all three PCMs enumerate automatically after reboot; AUX jack reports disconnected. MTKAIF calibration remains phases 3/3, cycles 3/3. |
| Host/build checks | Pass | 13 host tests, shell syntax, whitespace checks, cross-compilation of the kernel/modules/DTB and repeated application of the kernel patch series. |

Local artifacts are in `release/speaker-20261123/` (ignored by Git):
`boot-begonia-speaker-v3.img`, `modules.tar.gz`, `audio-userspace.tar.gz`,
`SHA256SUMS.txt`, `acoustic-test.txt`, `aux-connected.txt`, `switch-test.txt`,
`speaker-unplugged.txt`, `after-switch-playback.txt`, `reboot-final.txt` and
`after-reboot-playback.txt`.

The boot image SHA-256 is
`db583cbe6db178b23f419782ecf21aa03493096e8670993d58413c9dae0cdb75`.
It requires the matching rootfs modules and audio profiles. The prior working
boot is backed up on the handset as `/root/boot-before-speaker.img`, with the
previous audio modules/profiles under `/root/begonia-audio-before-speaker/`.
The final verified boot ID is `508d61c9-1b14-4074-be36-be6761d96016`, with
`begonia_speaker_output` and `begonia_internal_mic` selected by default.

Speaker testing covers basic playback and jack switching. Xiaomi's host DSP
speaker protection, sustained high-volume use, thermal endurance and
suspend/resume have not been qualified.

## Original AUX and peripheral test — 2026-11-22

The corrected audio boot image was flashed to the connected, unlocked Redmi
Note 8 Pro through fastboot. Matching kernel modules, ALSA/PipeWire packages,
UCM2, WirePlumber rules and `begonia-mic-aux-test` were installed into its existing
Arch root filesystem over USB SSH. Userdata was preserved.

The phone was left running Arch Linux ARM, kernel 6.16.4, with the default
PipeWire source `begonia_internal_mic` and sink `begonia_aux_output`.

| Peripheral/check | Result | Evidence and scope |
| --- | --- | --- |
| USB network and SSH | Pass | USB interface, 172.16.42.1, repeated SSH connections and file transfers after flashing/rebooting. |
| AUX + internal mic, PipeWire | Pass | Exactly 240000 stereo frames at 48 kHz captured and played. User confirmed clear recorded voice through both AUX channels. |
| AUX + internal mic, direct ALSA | Pass | Five-second S16_LE stereo capture on PCM 1 followed by playback on PCM 0; prior mixer state restored. |
| Simultaneous playback/capture | Pass, basic duplex | Low-level alternating stereo tone played while 240000 mic frames were captured; both processes completed in 5.193 seconds, without clipping in the recording. |
| AUX unplug/replug | Pass | Jack control recovered to connected; the subsequent voice test passed with the internal mic still selected. |
| Audio after reboot with AUX inserted | Pass | Card and connected jack appeared automatically; MTKAIF calibrated at phases 3/3, cycles 3/3. PipeWire record/playback completed again. |
| Touchscreen | Pass, basic input | Touch-down/up, tracking ID and X/Y position events captured from `spi1.0`. Four-corner accuracy and multitouch were not assessed. |
| Volume buttons | Pass | Press/release events for Linux key codes 114 and 115 from `mt6779-keypad`. |
| Display | Pass, console path | Active 1080×2340 DRM framebuffer; captured framebuffer shows the Arch login prompt. Backlight reports 1879/1879. |
| USB charging | Detected | MT6360 reports online and Charging, with a 5 V USB supply. Charging rate and battery endurance were not measured. |
| Accelerometer/gyro | Readings available | BMI120 exposes nonzero acceleration and angular-velocity readings. Calibration was not tested. |
| Compass/proximity | Readings available | Magnetometer X/Y/Z and STK3310 proximity samples read successfully. Accuracy was not tested. |
| GPU | Driver enumerates | Panfrost initializes Mali-G76 and exposes `renderD128`; no rendering workload was run. |
| Internal Wi-Fi/Bluetooth | Unavailable | No Wi-Fi interface or HCI controller enumerated; only loopback and USB networking are present. These remain separate plan steps. |
| System services | Pass | Zero failed system units in the final running state. |

The final verified boot was `de1c482b-ea27-4d6f-97a2-84241fa781bf`.
The boot image SHA-256 is
`b5611fe4f315e8ffbf94845a68f59765779cb2b02e6d46f9833d3848e002f726`.
That boot image requires the updated two-lane audio module and userspace files;
the boot image alone does not contain those rootfs changes.

## Fixes established by hardware testing

- Reference the audio domain through `spm` with its domain index; the domain
  child node is not a provider in this kernel.
- Enable the codec's VAUD18 supply for the board sound card.
- Require calibration of the two primary MTKAIF lanes. Both pass at phase 3;
  the unused third lane never transitions and previously prevented registration.
- Use the exported ALSA `iface=CARD,name='AUX Jack'` control in UCM and the test.
- Use nested WirePlumber `actions` / `update-props` objects. This makes the
  intended fixed node names and formats take effect.
- Handle PipeWire 1.6.11's status 1 at the requested recording sample limit only
  when the resulting WAV validates as a complete five-second recording.

## Artifacts and remaining qualification

Local artifacts and raw logs are under `release/audio-20261122/` (ignored by Git):

- `boot-begonia-audio-v2.img`, `modules-6.16.4.tar.gz`,
  `audio-userspace.tar.gz` and `SHA256SUMS.txt`.
- `mic-aux-pipewire.txt`, `mic-aux-alsa.txt`, `after-reboot-audio.txt`,
  `reboot-check.txt`, `final-state.txt`, `input-events.json` and `framebuffer.png`.
- `duplex-check.json`: concurrent playback/capture completed with 240000 recorded
  frames, playback status 0 and captured peak 3144/32768.
- `host-tests.txt`: 12 host checks passed. Kernel/modules/DTB cross-compilation
  also passed.

Speech intelligibility during simultaneous car-volume playback, channel-isolated speaker
mapping, charging noise at normal car volume, echo cancellation, suspend/resume,
thermal endurance, camera, modem and full GPU acceleration are not qualified by
this test. The basic voice replay confirmation does not establish those results.
