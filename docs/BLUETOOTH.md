# Onboard Bluetooth on begonia

The repository now builds `hci_stp.ko`, a Linux HCI bridge over the existing
vendor WMT/STP/BTIF transport. The head-unit image installs BlueZ and its tools,
Bluetooth LE and RFCOMM support, and `begonia-bluetooth.service`.

## Implementation

- `boot/kernel-overlay/drivers/misc/mediatek/connectivity/common/hci_stp.c`
  registers an HCI device parented to the onboard `18002000.consys` device.
- WMT function-on/off controls only the Bluetooth function. STP task 0 carries
  H4 command, event, ACL and SCO packets; Linux's H4 receiver reassembles partial
  frames and validates lengths. Transmit work preserves order and retries a
  full STP window with a bounded queue and retry count.
- Callback unregister waits for in-flight STP and WMT reset notifications.
  Close cancels workers, frees partial RX and queued TX data, then powers down
  Bluetooth. A WMT reset ends in an HCI hardware-error notification so Linux
  can recover the controller; forced shared reset/resume is not qualified.
- The firmware advertises Synchronization Train support but returns Unknown
  HCI Command for `0x0c77`. A controller-specific quirk skips that initialization
  command, preserving the remaining extended features, including Secure
  Connections. The first failed initialization and corrected run were captured
  with `btmon`.
- The startup helper reduces BTIF's default per-packet debug logging.

The transport APIs come from the pinned connectivity port already used by
Wi-Fi. The address command `0xfc1a` follows the
[Linux MediaTek implementation](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/bluetooth/btmtk.c?h=v6.16.4).
This bridge uses the queue API rather than the vendor stack's direct BlueZ RX
callback. No Android Bluetooth userspace or external adapter is needed.

## Startup and identity

`begonia-bluetooth.service` runs after shared connectivity initialization and
before `bluetooth.service`. Workflow 02 installs and enables both services.
State and link keys remain under `/var/lib/bluetooth/` in the root filesystem.

The firmware initially reports the common address `00:00:46:76:63:01`. To avoid
sharing that identity, the helper generates and saves one host-assigned address
in `/etc/bluetooth/begonia-bdaddr` (root-only, mode 0600). The driver programs
that address at each Bluetooth power-on using the MediaTek vendor command.
This is not recovery of the handset's factory Bluetooth address. If the factory
address is known, provision it in that file before first pairing. Changing the
address later requires pairing again. Keep the file with the device's pairing
state when restoring the root filesystem.

```sh
sudo systemctl enable --now begonia-bluetooth bluetooth
bluetoothctl list
bluetoothctl show
sudo btmgmt info
readlink -f /sys/class/bluetooth/hci0
```

For manual pairing, keep an interactive agent open:

```sh
sudo bluetoothctl
# Inside bluetoothctl:
power on
agent KeyboardDisplay
default-agent
system-alias "Begonia Android Auto"
pairable on
discoverable on
# Select this controller on the source phone; compare and confirm its code.
# After pairing, substitute the source phone's actual address:
trust AA:BB:CC:DD:EE:FF
discoverable off
quit
```

LIVI's pairing UI and phone-call profile registration remain separate work.
A paired phone need not remain connected when no Bluetooth profile is in use.
RFCOMM control support does not establish a working SCO voice route.

## Handset results — 2026-11-24

Tested on the attached Redmi Note 8 Pro using kernel `6.16.4-begonia-wifi`
and updated rootfs modules (`wmt_drv`, `hci_stp`, Bluetooth core, HIDP and RFCOMM).
The boot image was unchanged. The running kernel's embedded config predates
the new Bluetooth options; the tested replacement Bluetooth modules were built
with LE and RFCOMM enabled. A complete matching kernel/module build is also
available separately, described below.

| Check | Result |
| --- | --- |
| HCI device | `/sys/devices/platform/soc/18002000.consys/bluetooth/hci0` |
| BlueZ | Controller enumerates, powers on, supports BR/EDR and LE |
| Stable test address | `02:A8:38:82:6D:0A`, saved per device |
| Discovery | Found the Nothing Phone (3a) Pro over BR/EDR |
| Pairing | Numeric comparison confirmed by the user; authenticated P-256 link key stored; BlueZ reports bonded and paired |
| ACL / service discovery | Source phone's SDP services resolved |
| RFCOMM | Connected to the source phone's HFP Audio Gateway, channel 4; `AT+BRSF=0`, `AT+CIND=?`, `AT+CIND?` and `AT+CMER=3,0,0,1` all returned `OK` |
| Wi-Fi coexistence | The 5180 MHz SAE AP remained active during Bluetooth discovery, pairing and RFCOMM control; simultaneous client traffic remains unqualified |
| Reboot persistence | Boot `0229bc46-11e8-4abd-807a-70a04c619d91`: Bluetooth and AP started automatically, address and trusted bond persisted; SDP and HFP RFCOMM reconnected without pairing again |
| Final system health | No failed system units and no HCI initialization error, kernel warning or call trace in the final boot |

The first numeric-confirmation attempt expired while waiting for the reply;
the second paired successfully. The source phone disconnected after service
discovery when no application profile was held open; the explicit RFCOMM test
subsequently established and closed its own control connection.

SCO audio, calls, suspend/resume, forced controller reset recovery, sustained
simultaneous AP/Bluetooth traffic and LIVI handover are not yet qualified.

## Artifacts and recovery

Handset logs are under `/home/alarm/bluetooth-test/`. Packet captures contain
pairing material; keep them private. Local release artifacts and copied logs
are under `release/bluetooth-20261124/` (ignored by Git).

A separate full build produces `6.16.4-begonia-bluetooth`, with LE/RFCOMM/HCI
configuration enabled in the kernel configuration. Its boot image was repacked
with the existing header and firmware, the explicit begonia DTB and the new
release's early-boot modules. Repacking verifies all payloads and header fields.
`boot-begonia-bluetooth.img`, `modules.tar.gz`, `bluetooth-userspace.tar.gz` and
`SHA256SUMS.txt` are a matched local installation set; this boot image has not
been flashed or boot-qualified in this test. Never install that boot image
without its matching modules and startup files.

Before testing, the previous WMT and Bluetooth modules were saved on the phone
in `/home/alarm/bluetooth-test/backup/previous-modules.tar.gz`. To revert the
module-only test installation:

```sh
sudo systemctl disable --now begonia-bluetooth bluetooth
sudo tar -xzf /home/alarm/bluetooth-test/backup/previous-modules.tar.gz \
    -C /usr/lib/modules/6.16.4-begonia-wifi/kernel
sudo depmod -a 6.16.4-begonia-wifi
sudo reboot
```

Do not unload `wlan_gen4m` to reload the shared transport: the existing Wi-Fi
driver crashed during that operation in this test and required a physical
restart. Its trace is saved as `wifi-unload-failure.txt`. Bluetooth-only unload
and reload succeeded while the AP remained active. No forced module unloads
were used.

Validation includes full kernel/modules/DTB cross-compilation, module dependency
checks, boot payload/header verification, clean and repeated patch application,
28 host tests, shell syntax checks and a clean kernel style check for the bridge.
