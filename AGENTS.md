# AGENTS.md

`README.md` is the human front page; this one is for agents. Read it first.

## State

The only code is `src/main.c`, which prints one line: no transport, no frame
builder, no SPI and no firmware path. The Makefile, `.clang-format` and
`.clang-tidy` are in place and their gates pass, so the skeleton a module plugs
into is real. `include/` appears with the first module, `tests/` with milestone
1 (transport plus `dump`).

## Layout

| Path | Holds |
| --- | --- |
| `src/` | C sources, one module per file pair; currently `main.c` only |
| `include/` | the matching `X.h`, with the project's documentation (planned) |
| `tests/` | unit tests over recorded frames and image files, no radio needed (planned) |
| `tools/` | helper scripts around the binary — fixture capture, analysis (planned) |
| `docs/` | reference documents, once the protocol outgrows the headers (planned) |
| `obj/`, `bin/` | build output, gitignored |

One binary, `bin/rt4d-cli`. Modules still to come: `uart.c` (open/close the port,
byte accurate read/write, timeouts), `frame.c` (frame building, checksum, ACK
versus timeout versus corruption), `spi.c` (SPI region table, dump, restore),
`firmware.c` (MCU erase and block writes), `logging.c` (stderr `log_*`).

The layout, plain Makefile and clang gates follow the local `rxx_lora` project
(`~/Desktop/GitHub/rxx_lora`), so the two repos are navigable alike.

## Build and gates

`make` compiles `src/*.c` under `-std=c11 -Wall -Wextra -Wpedantic` into
`bin/rt4d-cli`; `make help` lists the rest. Style is fixed by `.clang-format`
(Allman, 4 spaces, 100 columns) and `.clang-tidy` (bugprone, cert, misc and a
few readability checks), both taken from `rxx_lora`.

Before a commit: a clean `make`, `make fmt-check` and `make tidy`. `make test`
arrives with milestone 1. No gate builds or touches the radio.

## Protocol invariants

A frame is `[command][index hi][index lo][payload…][checksum]`; the checksum is
the low 8 bits of the sum of every preceding byte, and a firmware frame seeds
that sum with `0x48`. A successful write is answered with the single byte
`0x06`.

- A write index is the **chunk number**: `(byte offset - region start) / payload
  length`. Dividing by the frame length instead (payload + 3) is the bug that
  damaged the owner's radio; it must not come back in any form.
- A read index is a 1 KiB block number and reads are whole-image; writes are
  region-relative. Different notions, different variables.
- A read reply whose header byte is `0xFF` is an error, not data.
- ACK mismatch, checksum mismatch, timeout and short read are distinct failures
  with distinct messages; a timeout is never reported as a bad checksum.
- Region writes are the protocol's only write primitive, so a byte-range writer
  cannot be built; refuse any region that ends above `0x400000`.
- The region table is measured fact: it lives in `spi.c` / `include/spi.h` and
  changes only with a fresh dump and read-back as evidence.

## Constraints

- No radio access in tests, CI or any automated run: the gates work on recorded
  frames and image files, and a hardware run is always a human's decision.
- A write is never the first thing a session does. Dump first, read back after,
  and never program at or above `0x400000` — the runtime Query-ID database sits
  above the codeplug, and padding an image out to the chip size destroys it.
- Dependency policy: the aim is a build needing nothing beyond libc and POSIX; a
  new dependency has to earn its place.
- `horizon.md` is local scratch — gitignored, never committed, never cited from
  tracked docs. The durable facts it carries move into `docs/` and the headers,
  and the header text is the copy of record once it lands.

## Reference material outside this repo

- `~/Desktop/Forgejo/rt4d` — the firmware RE project that owns the radio facts:
  `SPI-FLASH.md` (region map, settings banks, calibration), `FLASHING.md` (MCU
  payload at `0x08002800`, bootloader bounds, PTT upgrade mode), `HARDWARE.md`
  (cable, pinmap, chips) and `references/` (prior art).
- `~/.local/bin/rt890-flash-fixed` — the patched Rust flasher that already
  restored the owner's radio; the behaviour to reproduce, superseded by this
  tool.

Read them rather than re-deriving: the arithmetic and the region map are exactly
the parts that go wrong when guessed at.

## Glossary

- **SPI flash** — the external part holding calibration, codeplug and image
  blobs; the tool's main subject.
- **codeplug** — the user-facing configuration: settings, channels, zones,
  contacts, call log.
- **region** — a flash range the bootloader exposes for writing under one id
  (`0x40`–`0xA3`), with its own offset and size.
- **chunk** — one 1 KiB write unit inside a region; its index is counted from
  the region start.
- **dump** / **restore** — a linear 4 MiB read into a file / writing the mapped
  regions back out of such a file.
- **MCU firmware** — the AT32F423 application image starting at `0x08002800`,
  never including the bootloader below it.
- **upgrade mode** — power-on with PTT held; the only mode that accepts MCU
  writes.

## Guiding principles

- Verified, not hoped-for: a successful exit code is not evidence, a read-back
  comparison is.
- Small and boring: one binary, one job per verb, no daemon, GUI or config file.
- Fail loudly and specifically — an unexpected difference is an error, never a
  warning to scroll past.
