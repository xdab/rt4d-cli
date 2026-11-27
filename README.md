# rt4d-cli

A simple command-line tool for the Radtel RT-4D handheld radio that reads,
writes and verifies SPI flash, and flashes MCU firmware.

## Why

Existing RT-4D tooling is unmaintained, GUI-only or quietly wrong. The widely
recommended one, the Rust `rt890-flash`, computes each write chunk's index from
the frame length instead of the payload, so a restore lands about three bytes
early per kilobyte: fonts, glyphs and voice prompts shift across the image and
the tail of every region is never written.

## Hardware

- Radtel RT-4D
- CH340 K1-jack cable at `/dev/ttyUSB0`, 115200 baud.
- SPI flash: the radio powered on in normal mode.
- MCU firmware: upgrade mode, i.e. power-on with **PTT** held (**MENU** enters
  the DMR-chip updater instead).

## Building

`make` builds `bin/rt4d-cli` with a C compiler and nothing beyond libc and
POSIX. `make help` lists the targets. `make test` drives the binary against a
modelled radio over a pseudo-terminal — no hardware, no root, no dump — and the
gates are `make fmt-check` (clang-format for the C, ruff for the Python) and
`make tidy` (clang-tidy); the C build needs none of the three.

## Usage

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --check
INFO: opened /dev/ttyUSB0: 115200 baud, 8N1, no flow control, 5000 ms reply deadline
INFO: closed /dev/ttyUSB0

$ ./bin/rt4d-cli -p /dev/ttyUSB0 --backup -o rt4d-backup.bin
INFO: backing up 4 MiB from /dev/ttyUSB0 to rt4d-backup.bin
INFO: 1 of 4 MiB
...
INFO: 4194304 bytes in 376.8 s (10.9 KiB/s)
INFO: sha256 7dd15d02...
```

`sha256sum rt4d-backup.bin` prints the same digest, and `cmp` compares two
backups byte for byte. A backup that fails part way leaves the partial image on
disk and exits non-zero, so the exit code, not the file, says whether it is
complete.

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --firmware -i rt4d.bin
INFO: flashing rt4d.bin (251904 bytes) over /dev/ttyUSB0
INFO: the radio is in its firmware mode
INFO: MCU flash erased
INFO: writing 246 blocks of 1024 bytes
...
INFO: 246 blocks acknowledged; MCU flash cannot be read back, so the count is the evidence
```

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --restore -i rt4d-backup.bin
INFO: restoring rt4d-backup.bin (sha256 7dd15d02...) to /dev/ttyUSB0
INFO: the radio already holds this image at block 0
INFO: writing 2236416 bytes in 2184 chunks; calibration skipped (region 0x40 differs between units)
...
INFO: read back 2236416 bytes in 240.1 s: identical
INFO: 1957888 bytes have no region command and were left alone (12 ranges)
```

## Restore

A dump is linear; a restore is not. The radio accepts writes only per region,
addressed by the region's command byte and by a chunk index inside it, and the
19 regions it knows cover 2,236,416 bytes — a little over half the image. Every
byte of that half is written and then read back and compared; the other half is
the radio's own backup banks and live message records, which no command can
address, so those ranges are left as they are and listed at the end.

The calibration sector is region `0x40` and differs between units, so a restore
skips it unless `--calibration` is given. Writing it is how a radio's own
calibration is put back.

## Firmware

`--firmware -i FILE` writes an MCU image through the radio's bootloader: switch
the radio off, then on with PTT held. The image is at most 251,904 bytes and a
shorter one is padded with zeros, because the whole region is always written: a
probe, two erase frames, then 246 blocks of 1 KiB, each acknowledged.

MCU flash cannot be read back, so a flash is checked by what it is — one
acknowledgement per block, and a count. The tool says that rather than implying
a comparison it did not make, and an interrupted run says the MCU may hold a
partial image.
