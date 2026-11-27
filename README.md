# rt4d-cli

A command-line tool for the Radtel RT-4D handheld radio that reads, writes and
verifies its SPI flash — and, later, flashes MCU firmware — proving every write
by reading it back.

## Status

The repository is a skeleton: `src/main.c` prints one line, and there is no
protocol, no serial port and no radio code yet. The milestones below are what
turns it into a tool.

| # | Milestone | Scope |
| --- | --- | --- |
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
the tail of every region is never written. This tool gets that arithmetic right
and proves it by reading back. It is also the flashing layer the sibling
firmware-rebuild project lacks.

## Hardware

- CH340 K1-jack cable at `/dev/ttyUSB0`, 115200 baud.
- SPI flash: the radio powered on in normal mode.
- MCU firmware: upgrade mode, i.e. power-on with **PTT** held (**MENU** enters
  the DMR-chip updater instead).

## Safety

Radio writes are a human operation. The tool never writes on its own, every
write is preceded by a dump and followed by a read-back, and nothing at or above
`0x400000` is ever written — that area holds the Query-ID database the firmware
reads at runtime.

Verification compares only the flash regions the bootloader exposes for writing;
the rest of the image cannot be addressed by the protocol and is expected to
differ.

## Building

`make` builds `bin/rt4d-cli` with a C compiler and nothing beyond libc and
POSIX. `make help` lists the targets; `make fmt-check` and `make tidy` are the
formatting and static-analysis gates.

## Usage

```sh
$ ./bin/rt4d-cli
hello world
```

The program prints a fixed line. `dump`, `restore` and the firmware verbs arrive
with the milestones above.

## License

No license file yet: this project is currently not licensed for reuse.
