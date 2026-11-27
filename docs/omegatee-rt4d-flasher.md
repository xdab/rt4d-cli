# omegatee/RT-4D_Flasher

The Radtel flash tool for Linux as published by omegatee: a LabVIEW binary shipped
as an installer, plus the stock firmware image it flashes. There is no source in
this repository, and the protocol cannot be read out of the artifact — what the
project does provide is ground truth about image format and boot modes.

Upstream: <https://github.com/omegatee/RT-4D_Flasher>, GPL-3.0.

## What is in the repository

| Path | Size | What it is |
| --- | --- | --- |
| `4DFlash_install.zip` | 701,539 | The GUI "installer": the app, its config, the NI LabVIEW 2023 runtime and NI Linux device drivers |
| `FW/V3_12_241206.bin` | 217,516 | Stock MCU image V3.12, as the GUI flashes it |
| `README.md` | — | Screenshot, boot sequences, manufacturer link |

`4DFlash_install.zip` holds five files: `4DFlash` (ELF x86-64, stripped, 67,204
bytes), `4DFlash.conf`, `4DFlash.aliases`,
`ni-labview-2023-runtime-engine-23.3.5-linux.zip` and
`NILinux2023Q4DeviceDrivers.zip`. Running the tool therefore means unpacking and
installing a National Instruments runtime and driver tree under NI's licence, not
just the 67 KB executable.

## How the artifact is put together

`4DFlash` is a LabVIEW runtime-engine launcher, not the application: its strings
name `LVRTMain`, `liblvrt.so.23.3`, `liblvrtdark.so.23.3`, the `ni.com/rteFinder`
download URL, and a `.comment` naming
`lvapp_bcrte_linuxU_x64_gcc-6.2_release_23.3.0f269` — a LabVIEW 2023 Q3 build. The
application itself sits in data appended after the ELF's section header table,
starting with the `RSRC` and `LVARLBVW` resource-library magic. Those 52,484 bytes
are opaque: the header, then no plain-text strings and no zlib stream anywhere in
it, so the protocol constants — the command bytes, the checksum seed, the block
size — cannot be confirmed from the artifact. Reading them would mean extracting
the VIs.

Practically, that makes this project a **format and boot-mode witness**, not a
protocol reference.

## What it establishes

The flasher's input is the raw application image, not an Intel-HEX or a container:
`FW/V3_12_241206.bin` is 217,516 bytes with SHA-256 `816876f4d0cae310…`,
byte-identical to the payload extracted from the Radtel Windows upgrader
`RT-4D Upgrade V3_12_241206.exe`. Two separately distributed tools ship the same
bytes, so 0x08002800-relative raw images are the format to accept.

Boot modes are per model and differ:

| Model | MCU firmware | Other |
| --- | --- | --- |
| RT-4D | PTT held at power-on | MENU = DMR firmware update; LEFT = copy master; RIGHT = copy slave |
| RT-890 | both side keys held at power-on | — |
| RT-860 | PTT held at power-on | — |

217,516 bytes fits the bootloader's `0x3D800` writable region, so the largest
stock image in this repository still leaves the region partly unused.

## Position against this project

The tool is a black box: no CLI, no scripts, no dump or read path known, no way to
verify a write, and an install chain (NI runtime + drivers) far heavier than the
cable it talks to. It is not a candidate to reimplement or wrap. Its value is the
byte-identical image and the boot-mode table above, both of which the firmware
milestone leans on when it checks an image before flashing: raw bytes, starting at
`0x08002800`, no larger than `0x3D800`.
