# rt4d-cli

A simple command-line tool for the Radtel RT-4D handheld radio that reads,
writes and verifies SPI flash, and flashes MCU firmware.

## Status

The port, the frame layer and the whole-image `--backup` are in place: every
reply is checked and every block is retried before the run fails. Restore and
the firmware path are not written yet. Background on the existing third-party
implementations is in [`docs/`](docs/README.md).

| # | Milestone | Scope |
|---|-----------|-------|
| 1 | transport | frame building, checksum, ACK/timeout classification — done; tests over recorded frames still to come |
| 2 | `backup` | linear 4 MiB read, per-block checksum, progress, SHA-256 — done |
| 3 | `restore` | region-at-a-time write with `--verify` read-back |
| 4 | `firmware flash`, `firmware backup` | MCU erase and 1 KiB block writes; backup is conditional on the bootloader exposing MCU reads at all |
| 5 | harness | backup, restore, backup; compare the regions the protocol can write |

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
POSIX. `make help` lists the targets; `make fmt-check` and `make tidy` are
formatting and static-analysis gates.

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
