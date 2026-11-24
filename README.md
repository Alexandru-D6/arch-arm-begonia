# Arch Linux ARM para Xiaomi Redmi Note 8 Pro (begonia)

> **Arch Linux ARM** corriendo como sistema operativo nativo en begonia, portado desde el **hardware enablement de postmarketOS** — kernel mainline, DTB, módulos y firmware — pero reemplazando completamente Alpine/musl por **glibc/pacman/systemd**.

## 📋 Arquitectura

```
Arch Linux ARM begonia
├── boot.img
│   ├── Kernel mainline 6.x (linux-postmarketos-mediatek-mt6785)
│   ├── DTB/DTBO correcto para begonia (MT6785 Helio G90T)
│   └── initramfs de pmOS (busca rootfs por label pmOS_root)
│
└── rootfs.img
    ├── Arch Linux ARM aarch64
    ├── glibc (libc 2.42+)
    ├── pacman
    ├── systemd (multi-user.target)
    ├── LIVI 10.5.0 (kiosk headless en tty1, seatd + PipeWire)
    ├── openssh
    ├── NetworkManager + tailscale
    └── módulos del kernel pmOS + firmware mediatek
```

### NO es ❌

- Arch dentro de postmarketOS
- chroot / proot / contenedor
- Alpine host + Arch userspace
- dualboot
- Termux

### SÍ es ✅

- Rootfs real Arch Linux ARM aarch64
- `/sbin/init` → systemd de Arch
- `pacman` → glibc
- USB OTG / dongle Ethernet USB funcionales
- Batería real (FGU MT6360 desde vendor kernel — opcional)

## 🧬 Origen

Este proyecto se inspira en el port **"Nothing Phone (1) Spacewar - ALARM"** (Arch Linux ARM Red Moon), que corre Arch Linux ARM aarch64 sobre el kernel y habilitación de postmarketOS.

El mismo enfoque se aplica aquí:

```
pmOS kernel + dtb → boot.img
Arch Linux ARM rootfs → systemd + glibc + pacman
Initramfs pmOS → montaje de pmOS_root
```

## 🏗️ Pipeline de construcción

### Workflow 1: Build pmOS base (`01_build_pmos_base.yml`)

Descarga un artefacto de begonia del productor indicado en `source_repo`. Con `source_run` vacío busca una compilación exitosa con el artefacto solicitado. Ejecútalo en este repositorio antes del workflow 02. Extrae:
- `boot.img` — kernel mainline 6.x + DTB + initramfs pmOS
- `modules.tar.gz` — módulos del kernel compilados
- `firmware.tar.gz` — firmware mediatek/MT6785
- Parámetros de arranque en la cabecera de `boot.img`

Habilita drivers USB Ethernet (AX88179, RTL8152, SMSC95XX) y WiFi Realtek/MediaTek para soporte de dongles.

### Workflow 2: Build Arch ARM (`02_build_alarm_begonia.yml`)

Construye la imagen final:
1. Descarga `ArchLinuxARM-aarch64-latest.tar.gz`
2. Crea rootfs ext4 de 8 GB
3. Compila el kernel y el DTB de begonia e instala sus módulos y el firmware de pmOS
4. Configura systemd (multi-user, sshd, NetworkManager) e instala LIVI 10.5.0 ARM64 con SHA-256 verificado, seatd y PipeWire para arrancar su kiosk en tty1
5. Reconstruye boot.img con el kernel y DTB compilados, la cabecera v2 y el initramfs de pmOS con los módulos de arranque de esa misma compilación, incluidos el regulador MT6360 y el controlador USB-C necesarios para la red USB
6. Publica `boot-begonia-arch.img` + `rootfs-archlinuxarm-begonia-console.img.xz`

## 📥 Descarga

El workflow 02 publica las imágenes bajo la etiqueta `nightly` en la sección Releases de este repositorio.

| Archivo | Descripción |
|---|---|
| `boot-begonia-arch.img` | Boot image (kernel + DTB de begonia + initramfs pmOS) |
| `rootfs-archlinuxarm-begonia-console.img.xz` | Rootfs Arch Linux ARM con LIVI headless (8 GB, nombre conservado por compatibilidad) |
| `SHA256SUMS.txt` | Checksums |

## 🔧 Instalación

### Requisitos

- Redmi Note 8 Pro (begonia) con bootloader desbloqueado
- PC con fastboot y cable USB
- ~10 minutos

### Pasos

```bash
# 1. Descargar ambos archivos de la misma release nightly

# 2. Decomprimir rootfs (si es .xz)
xz -d rootfs-archlinuxarm-begonia-console.img.xz

# 3. Bootear a fastboot
adb reboot bootloader
# o: apagar → Volumen ABAJO + ENCENDIDO

# 4. Verificar conexión
fastboot devices

# 5. Flashear boot (kernel + initramfs)
fastboot flash boot boot-begonia-arch.img

# 6. Flashear rootfs (reemplaza todos los datos de userdata)
fastboot flash userdata rootfs-archlinuxarm-begonia-console.img

# 7. Reiniciar
fastboot reboot
```

### Primer arranque

El initramfs de pmOS busca el sistema ext4 con la etiqueta `pmOS_root` en `userdata` y arranca systemd de Arch. LIVI se inicia automáticamente en tty1 sin escritorio ni login; SSH sigue disponible. La compilación elimina los UUID del sistema de referencia. [Configuración, diagnóstico y vuelta al modo consola](docs/LIVI.md).

Para conectarse:
```bash
# USB Networking (PC)
USB_IF=enp0s20f0u1  # sustituir por la interfaz USB que aparece en el PC
sudo ip addr replace 172.16.42.2/24 dev "$USB_IF"
sudo ip link set "$USB_IF" up
ssh alarm@172.16.42.1  # dirección del teléfono, heredada del initramfs pmOS

# USB Ethernet dongle (conecta a router)
ssh alarm@<IP>

# Credenciales
# Usuario: alarm
# Contraseña: alarm
```

Post-install:
```bash
# Inicializar keyring de pacman
pacman-key --init
pacman-key --populate archlinuxarm

# Configurar red
systemctl enable --now NetworkManager
nmtui  # o: nmcli dev wifi connect SSID password "PASS"

# Tailscale
systemctl enable --now tailscaled
tailscale up --accept-routes
```

## 📊 Estado del hardware

| Componente | Estado | Notas |
|---|---|---|
| **Booteo** | ✅ | Initramfs encuentra rootfs |
| **Apagado** | ✅ | MT6359 PWRHOLD: arranque con pulsación normal de 2–3 s tras apagar. [Detalles](docs/POWER_OFF.md) |
| **Carga con el teléfono apagado** | ✅ | USB carga con la pantalla apagada; Arch espera a mantener pulsado Power y soltarlo tras la vibración. [Detalles](docs/CHARGE_ONLY.md) |
| **Kernel** | ✅ | Mainline 6.16.4+ |
| **RAM** | ✅ | 6 GB completos |
| **Almacenamiento** | ✅ | Partición userdata (64/128 GB) |
| **USB OTG** | ✅ | Ethernet dongles, teclados |
| **USB Ethernet** | ✅ | AX88179, RTL8152, SMSC95XX |
| **WiFi dongle** | ✅ | RTL8192EU (TP-Link WN821N) |
| **Pantalla** | ✅ | Framebuffer console |
| **Touchscreen** | ✅ | Input multitouch |
| **GPU** | ✅ | Panfrost (Mali-G76) |
| **WiFi interno** | ✅ | MT6785/WMT + `wlan_gen4m`: conexión WPA2 a Guest1, DHCP, DNS e Internet verificados. [Instalación y resultados](docs/WIFI.md) |
| **Bluetooth** | ✅ básico | HCI sobre WMT/STP/BTIF: BlueZ, emparejamiento y control RFCOMM verificados con AP de 5 GHz activo. Audio SCO y LIVI pendientes. [Detalles](docs/BLUETOOTH.md) |
| **Audio** | ✅ | AUX estéreo + micrófono interno verificados con ALSA/PipeWire. Altavoz interno automático al desconectar AUX, probado en el teléfono. Pendiente de validación prolongada en coche. [Resultados](docs/AUDIO_TEST_RESULTS.md) · [Prueba de 5 segundos](docs/AUDIO.md) |
| **LIVI** | Arranque verificado | Kiosk headless 10.5.0 inicia tras reiniciar, sustituye el terminal en tty1 y muestra la UI con Panfrost. Proyección pendiente. [Detalles y prueba](docs/LIVI.md) |
| **Cámara** | ❌ | En desarrollo |
| **Módem 4G** | ❌ | MT6779 — sin driver mainline |
| **Batería (FGU)** | ⏳ | En port desde vendor kernel |

## 📁 Estructura del repo

```
alarm-begonia/
├── .github/workflows/
│   ├── 01_build_pmos_base.yml      # Extraer pmOS boot/modules/firmware
│   └── 02_build_alarm_begonia.yml  # Construir Arch ARM final
├── initramfs/
│   ├── init                        # Initramfs mínimo Arch
│   ├── build-initramfs.sh          # Script para crear initramfs
│   └── files.list                  # Archivos a incluir
├── rootfs/
│   ├── prepare-rootfs.sh           # Preparación del rootfs
│   ├── install-livi.sh             # Instalación offline del kiosk LIVI
│   ├── packages.txt                # Paquetes a instalar
│   ├── services.sh                 # Configuración de servicios
│   └── firstboot.sh                # Primer arranque
├── boot/
│   ├── repack-bootimg.sh           # Reconstruir boot.img
│   └── cmdline.txt                 # Ejemplo para el initramfs alternativo
├── flash/
│   └── flash-begonia-arch.sh       # Script de flasheo
└── README.md
```

## 🥭 Créditos

- **postmarketOS** — habilitación de hardware para begonia
- **Nothing Phone (1) ALARM** — inspiración del port
- **Arch Linux ARM** — rootfs base
- **Kupfer Linux** — documentación del enfoque Arch en dispositivos móviles
- **Vivi AI** — pipeline de construcción automatizado
- Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)

## 📜 Licencia

MIT — construido sobre software libre de la comunidad. Los archivos de LIVI
en `rootfs/livi/upstream/` conservan GPL-3.0-or-later y su `NOTICE` de atribución.
