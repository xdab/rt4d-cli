# rt4d-cli

A simple command-line tool for the Radtel RT-4D handheld radio that reads,
writes and verifies SPI flash, and flashes MCU firmware.

## Status

The repository is a skeleton: there is no protocol, no serial port and no radio code yet.
The milestones below are what turns it into a tool.
Background on the existing third-party implementations is in [`docs/`](docs/README.md).

| # | Milestone | Scope |
|---|-----------|-------|
| 1 | transport | frame building, checksum, ACK/timeout classification, tests over recorded frames |
| 2 | `dump` | linear 4 MiB read, per-block checksum, progress, final SHA-256 |
| 3 | `restore` | region-at-a-time write with `--verify` read-back |
| 4 | `firmware flash`, `firmware backup` | MCU erase and 1 KiB block writes; backup is conditional on the bootloader exposing MCU reads at all |
| 5 | harness | dump, restore, dump; compare the regions the protocol can write |

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
$ ./bin/rt4d-cli
hello world
```
