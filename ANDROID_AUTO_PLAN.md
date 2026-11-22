# Wireless Android Auto on the Xiaomi Redmi Note 8 Pro

The Redmi runs Arch Linux ARM and LIVI as the dashboard display. A second Android phone runs the apps, supplies navigation and mobile data, and projects wirelessly. Audio goes from the Redmi’s 3.5 mm headphone jack to the car’s AUX input, falling back to the internal loudspeaker when AUX is disconnected. Voice comes from the Redmi’s built-in microphone, including while the AUX cable is plugged in. The car’s USB connection supplies power. The driver starts the Redmi manually and LIVI starts on boot.

The Redmi’s **built-in Bluetooth** pairs with the source phone and hands the session over to a **5 GHz access point hosted by the Redmi’s built-in Wi-Fi**. USB serves only as the car power input during projection. Once the image boots reliably, establish the shared MediaTek connectivity transport, then bring up the access point and Bluetooth controller. Audio development can proceed in parallel. [LIVI wireless architecture](https://github.com/f-io/LIVI/blob/v10.5.0/README.md#wireless)

## What the repository builds

| Area | Evidence at revision `bada2e5` | Consequence |
| --- | --- | --- |
| Boot and root filesystem | Workflow 01 downloads a pmOS artifact. Workflow 02 retrieves the base from a hard-coded external repository, rebuilds the MT6785 kernel, reuses the pmOS ramdisk and creates an ext4 filesystem labelled `pmOS_root`. | Use workflow 02 as the Arch image entry point. Its inline package and service steps need to consume the new LIVI profile. Editing only `rootfs/packages.txt` or `initramfs/init` will miss this path. [Base workflow](.github/workflows/01_build_pmos_base.yml) · [Arch workflow](.github/workflows/02_build_alarm_begonia.yml) |
| Kernel and device tree | Workflow 02 suppresses `modules_install` errors, looks under `mnt/` after changing into `linux-kernel/`, and takes the first DTB matching begonia **or** MT6785. The DTB repack can create two symlinks pointing at each other. Workflow 04 publishes only a boot image. | A successful compile or matching release string gives insufficient assurance that the device will boot the intended DTB and load the new drivers. Ship a matched boot image and rootfs. [Image builder](.github/workflows/02_build_alarm_begonia.yml) · [Kernel-only builder](.github/workflows/04_build_kernel_only.yml) |
| Hardware | The README reports display, multitouch, Panfrost and USB OTG working. It lists internal Wi-Fi, Bluetooth and audio as unfinished. Workflow 02 enables the MT6359 codec and claims speaker/jack support in its release text. | Treat the screen and OTG as a starting point. Require a physical jack playback and internal mic recording test before advertising audio. The bundled RTL8192EU driver targets an external dongle. [Hardware table](README.md) · [Kernel configuration and release text](.github/workflows/02_build_alarm_begonia.yml) |
| Application and video | The Arch image boots to a console. Workflow 03 builds a separate Kupfer image. Workflow 05 builds FFmpeg/OpenH264 libraries for neatvnc with hardware acceleration disabled. | Integrate LIVI’s own kiosk and GStreamer runtime in the Arch image. GPU rendering and sustained H.264 decoding need separate tests. [Kupfer workflow](.github/workflows/03_build_kupfer_begonia.yml) · [Codec workflow](.github/workflows/05_build_ffmpeg_openh264.yml) |

## Implementation order

### 1. Establish a recoverable begonia build

- On the handset, record the product, board and active panel/touch variant from stock or a known-working Linux image. Capture the working device tree, kernel config, partition sizes, USB roles and audio mixer state where accessible. CSOT and Tianma variants can require different touch firmware. Save a known-good boot image, the matching recovery materials and a backup of user data before using `userdata`. [Begonia panel bring-up](https://github.com/criollojoel10/begonia-pmos-tianma)
- In workflows 01/02, pin the pmOS inputs, kernel, config, toolchain and rootfs digest. Resolve the intended base artifact in this fork. Add a tracked begonia kernel configuration fragment and patch series. Build the kernel, selected DTB and modules from one tree. Install modules to an absolute staging path, run `depmod` for the built kernel release, and verify firmware and any early-boot modules inside the ramdisk.
- Repack using the known-good boot header and device-specific parameters, with an explicit DTB for the detected panel. Unpack the result and compare the kernel, DTB and ramdisk to the intended inputs. Verify the cmdline, root label and fstab together.
- Fix the flash helper and README together. Check image checksums, fastboot product, target device, bootloader state and partition sizes before writes. Explain that installation replaces `userdata`. Remove the README’s post-flash erase step, which would delete the installed root filesystem. Include a tested recovery procedure and matching filenames. [Flash helper](flash/flash-begonia-arch.sh) · [Current install instructions](README.md)

**Pass when** the exact handset cold-boots the paired images repeatedly, reaches Arch systemd, loads a module from that build, and provides a working maintenance connection. Record the live DT compatible, panel firmware and kernel release with the image hashes.

### 2. Bring up the internal Wi-Fi access point

- Integrate the begonia connectivity port at `3a1ea7694219eb2fd513f0e630f2a882eab5c86f` with its matching DT and firmware. Its maintainers report a successful 5 GHz client connection on a Redmi Note 8 Pro. Reproduce that baseline in Arch. Package the firmware at the paths the driver requests and add a boot unit to load `mtk-vendor-btif`, `wmt_drv` and `wlan_gen4m` in order, then activate Wi-Fi through `/dev/wmtWifi`. [Begonia hardware results and firmware details](https://github.com/criollojoel10/begonia-pmos-tianma) · [Connectivity port](https://gitlab.postmarketos.org/mt6785-mainline/linux/-/merge_requests/2)
- Identify the onboard interface with `iw dev` and sysfs. Use `iw list` to check supported modes and channels. Run hostapd on a locally permitted 5 GHz channel and dnsmasq on that interface. Keep NetworkManager from competing for it. If AP mode fails, capture the hostapd and kernel logs and trace the failing cfg80211 operation against the vendor AP implementation.

**Pass when** the source phone can discover and join the Redmi’s 5 GHz AP, obtain a DHCP lease and sustain local traffic after a cold boot. Record the channel and the Wi-Fi interface’s onboard device path. Repeat with Bluetooth active in step 3.

### 3. Expose the internal Bluetooth controller to Linux

The reference port reports that the shared WMT/BTIF transport can power the Bluetooth function, but lacks the Host Controller Interface (HCI) driver that BlueZ needs. This is the largest feasibility risk for wireless Android Auto on this kernel. [Begonia Bluetooth bring-up findings](https://github.com/criollojoel10/begonia-pmos-tianma#por-qu%C3%A9-el-bluetooth-interno-no-se-puede)

- Trace the stock begonia firmware initialization and Bluetooth packet path. Reuse the port’s shared transport and implement the HCI driver that registers the onboard controller with Linux. Bring up HCI command/event exchange first, followed by ACL data for Classic pairing and RFCOMM. Handle power, reset and resume so restarting Bluetooth preserves the running Wi-Fi AP.
- Identify how the controller carries SCO voice data on this board, including any separate PCM route. Capture controller events with `btmon` and verify the hands-free call path once the audio routes and LIVI are integrated. [LIVI Linux Bluetooth helper](https://github.com/f-io/LIVI/blob/v10.5.0/native/crates/livi-helper/src/bluez.rs)

**Pass when** BlueZ enumerates the onboard controller, the source phone bonds and reconnects after reboot, and Bluetooth exchanges data while the Wi-Fi AP carries traffic. Verify the HCI device’s sysfs parent and retain that mapping with the Wi-Fi results.

### 4. Bring up the AUX output and internal microphone

**Implementation:** the build now applies the tracked MT6785 DL1/VUL12/ADDA
driver, begonia audio DTS and MT6359 jack-detector fixes. Workflow 02 installs
the UCM2 and WirePlumber profiles and `begonia-mic-aux-test`. See
[audio setup and acceptance](docs/AUDIO.md). On 2026-11-22 the handset passed
five-second internal-mic recording and stereo AUX playback through ALSA and
PipeWire; the user confirmed clear voice on both channels. AUX replugging and
automatic audio startup after reboot were checked. See the
[hardware test report](docs/AUDIO_TEST_RESULTS.md). A five-second concurrent tone
playback/mic capture check also passed. Speech under car-volume playback,
charging noise and far-end echo remain to be qualified.

- Start with Xiaomi’s `mt6785-afe-pcm.c`, `mt6785-afe-clk.c`, `mt6785-dai-adda.c` and `mt6785-mt6359.c`. The vendor machine driver identifies the codec link and its MTKAIF calibration sequence. Compare these with the selected Linux kernel and reuse its ASoC and MT6359 drivers where available. Identify the missing audio front-end clocks, DMA/interrupts, pinctrl, power domains and codec links. Implement the minimal playback and capture paths in the kernel patch series and describe the board connections in the begonia DTS. [Xiaomi MT6785 audio sources](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/begonia-p-oss/sound/soc/mediatek/mt6785) · [Vendor machine driver](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/sound/soc/mediatek/mt6785/mt6785-mt6359.c)
- Expose a stereo headphone output and an independent handset mic input. Implement bias, gain, jack detection and routing for a three-pole AUX cable. Use the stock board routing as reference. Add a separate TFA9874/I2S3 speaker path and ALSA UCM2/WirePlumber policy that selects AUX when connected and the internal speaker when disconnected, while keeping the built-in mic selected. Basic speaker playback and automatic fallback passed on the attached handset on 2026-11-23; sustained speaker use remains unqualified. [Hardware results](docs/AUDIO_TEST_RESULTS.md) · [Xiaomi jack and amplifier definitions](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/arch/arm64/boot/dts/mediatek/xiaomi-mt6785-common.dtsi)

**Pass when** `aplay -l` and `arecord -l` expose the devices, left/right audio reaches the correct car speakers, and a simultaneous recording clearly picks up speech at the Redmi. Verify both routes after replugging AUX and rebooting. Repeat through PipeWire as the kiosk user. Check for charging noise and far-end echo at normal car volume.

### 5. Install LIVI into the Arch image

- Add a head-unit rootfs profile that workflow 02 actually executes. Pin `LIVI-10.5.0-linux-arm64.AppImage` and verify its release SHA-256, `5b3cb1bc85ff8a1720e49a710da386b4807a4009456d1dc7f73bb54751f2bb79`. Adapt the matching upstream headless installer templates to image creation. Its runtime actions that require a live seat or user session belong at boot. [Release](https://github.com/f-io/LIVI/releases/tag/v10.5.0) · [Release asset metadata](https://api.github.com/repos/f-io/LIVI/releases/tags/v10.5.0) · [Headless installer](https://github.com/f-io/LIVI/blob/v10.5.0/scripts/install/headless/install.sh)
- Resolve the upstream `core` and `lite` pacman dependencies for aarch64. Include FUSE3, seatd, libinput, Mesa/GBM/EGL, BlueZ, hostapd, dnsmasq, iw, PipeWire, pipewire-pulse and WirePlumber. Install the matching kiosk unit, PAM, udev and privileged helper configuration. Give the kiosk account seat, render, input and audio access. Keep its pairing state and LIVI settings persistent. Provision maintenance credentials per device. [Package manifest](https://github.com/f-io/LIVI/blob/v10.5.0/scripts/install/packages.txt) · [Kiosk unit](https://github.com/f-io/LIVI/blob/v10.5.0/assets/linux/livi-kiosk.service.template)
- Enable wireless Android Auto and select the onboard HCI and AP interfaces identified in steps 2 and 3. Configure the country/channel and give LIVI’s AP helper control of the Wi-Fi interface. Check pairing, wireless handover, DHCP and projection with the source phone unplugged. Verify the Linux helper can register the phone-call profile and open SCO while projection is active.
- Set landscape orientation and apply the corresponding touch transform. Test all four corners and multitouch. Start with a proposed 720p/30 fps H.264 target. Inspect LIVI’s actual GStreamer decoder selection and measure frame drops, heat and CPU use. If software decode cannot sustain the target, assess MT6785 VDEC, firmware and V4L2/GStreamer support as a distinct driver task.

**Pass when** a manual power-on reaches LIVI without a shell login, a paired phone reconnects, and the user can navigate Maps by touch, play media and issue a voice command through the Redmi.

## Release acceptance on the actual handset

| Test | Proposed acceptance |
| --- | --- |
| Installation and recovery | A fresh install reaches the kiosk. The documented rollback restores the known-good system. Archive the board variant, image hashes, onboard radio drivers and firmware revisions, charger, and source phone model/Android version. |
| Wireless operation | Use the Redmi’s built-in radios throughout. With the source phone unplugged and the Redmi connected directly to car USB power, complete first pairing, Bluetooth handover and projection on the Redmi’s 5 GHz AP. Repeat ten manual boot/reconnect cycles and one walk-away/return. The source phone retains mobile data while navigating. |
| Media and voice | Play music over AUX, hear navigation prompts, invoke the assistant, place and receive a call. Both parties hear speech and music resumes afterward. Disconnecting and reconnecting AUX restores the same output and internal mic. |
| Sustained use | Run a parked 60-minute navigation and audio session with the screen at intended brightness and car USB connected. Log negotiated video mode, dropped frames, audio underruns, temperature, battery trend and disconnects. Require stable projection, intelligible calls and enough input power to sustain the unit. |
| Release artifacts | Publish a versioned boot/rootfs pair, checksums, a supported hardware list, install/recovery instructions and the hardware test results for that exact pair. CI validates image contents and ABI. The parked test qualifies the install for the car. |
