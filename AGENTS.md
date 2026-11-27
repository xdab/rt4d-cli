# AGENTS.md

`README.md` is the human front page; this one is for agents.

## State

The SPI jobs are real: `uart.c` opens a raw 115200 8N1 port and moves exact byte
counts under a deadline, `frame.c` builds the `52` block read and the region
write frame and checks both replies, `spi.c` backs up the 4 MiB image block by
block and restores it region by region from a table of the commands the radio
knows, and `main.c` dispatches `--check`, `--backup` and `--restore`.
`firmware.c` writes an MCU image through the bootloader: probe, two erase
frames, then 246 acknowledged 1 KiB blocks. `logging.c` is the stderr logger;
`sha256.c` is Brad Conte's public-domain SHA-256, vendored whole with its
notice.
`tests/` drives the binary over a pseudo-terminal against a model of the radio's
flash, under unittest and ruff.

## Layout

| Path | Holds |
|------|-------|
| `src/` | C sources, one module per file pair: `main.c` (verb dispatch), `options.c`, `uart.c`, `frame.c`, `spi.c` (region table, backup, restore), `firmware.c` (bootloader probe, erase, block writes), `logging.c`, `sha256.c` (vendored) |
| `include/` | the matching `X.h`; the protocol rules live here and are the copy of record |
| `tests/` | a model of the radio's flash and the backup/restore tests, no radio needed |
| `tools/` | helper scripts around the binary — fixture capture, analysis (planned) |
| `docs/` | one summary per third-party implementation, plus its `README.md` index |
| `obj/`, `bin/` | build output, gitignored |

One binary, `bin/rt4d-cli`. `tools/` is the only planned directory still
missing; no protocol path is left unimplemented.

## Build and gates

`make` compiles `src/*.c` under `-std=c11 -Wall -Wextra -Wpedantic` into
`bin/rt4d-cli`; `make help` lists the rest. Style is fixed by `.clang-format`
(Allman, 4 spaces, 100 columns) and `.clang-tidy` (bugprone, cert, misc and a
few readability checks). The Python tests follow `ruff.toml`: the same 100
columns, ruff format plus ruff's default lint set, with two rules ignored
because they describe something these tests do not do. clang-format and ruff are
gate-time tools, never build dependencies of the binary.

Before a commit: a clean `make`, `make fmt-check`, `make tidy` and `make test`.
No gate builds or touches the radio.

## Protocol invariants

A frame is `[command][index hi][index lo][payload…][checksum]`; the checksum is
the low 8 bits of the sum of every preceding byte, and a firmware frame seeds
that sum with `0x48`. A successful write is answered with the single byte
`0x06`.

- A write index is the **chunk number**: `(byte offset - region start) / payload
  length`. Dividing by the frame length instead (payload + 3) misplaces every
  chunk of a restore; it must not come back in any form.
- A read index is a 1 KiB block number and reads are whole-image; writes are
  region-relative. Different notions, different variables.
- A read reply whose header byte is `0xFF` is an error, not data.
- ACK mismatch, checksum mismatch, timeout and short read are distinct failures
  with distinct messages; a timeout is never reported as a bad checksum.
- Region writes are the protocol's only write primitive, so a byte-range writer
  cannot be built; refuse any region that ends above `0x400000`.
- Writing index 0 of a region erases that region's whole span, so a region is
  written from its start and a wrong index erases the wrong data.
- Only the 19 commands in `spi.c`'s table are regions. Any other command byte is
  programmed at address 0 without an erase, clearing bits in the calibration
  sector; `make test` fails if one is ever sent.
- A wrong ACK byte is the radio refusing the chunk: stop and say so. A timeout or
  a short reply is a line fault: drain and retry.
- The bootloader's frames carry the same sum seeded with `0x48`, the probe
  included; the probe is the block-0 read, and a radio in firmware mode answers
  `0xFF` where a block would start `52`.
- Erase is two frames, `39 33 05 10` then `39 33 05 55`. What separates the
  halves is not known; both are sent because that order is known to work.
- A firmware write offset is the low 16 bits of the byte offset, so it wraps
  every 64 KiB and the bootloader counts blocks itself.
- MCU flash cannot be read back. A firmware flash is evidenced by one
  acknowledgement per block and nothing else; never describe it as verified.
- The region table is measured fact: it belongs in `spi.c` / `include/spi.h`, and
  it changes only with a fresh dump and read-back as evidence.

## Constraints

- No radio access in tests, CI or any automated run: the gates work on recorded
  frames and image files, and a hardware run is always a human's decision.
- A write is never the first thing a session does. Dump first, read back after,
  and never program at or above `0x400000` — the runtime Query-ID database sits
  above the codeplug, and padding an image out to the chip size destroys it.
- Dependency policy: the aim is a build needing nothing beyond libc and POSIX; a
  new dependency has to earn its place.

## Reference implementations

`docs/` holds one summary per upstream implementation of this protocol, indexed
by `docs/README.md`. They are third-party projects: not dependencies, not vendored,
not part of this repository. A claim about one of them belongs in the matching
file, with the code path that shows it. Read the summary before the upstream
source, and read either rather than re-deriving — the index arithmetic and the
region map are exactly the parts that go wrong when guessed at.

The protocol rules the tool implements are owned here, in `include/`, not in
`docs/`; that text is the copy of record.

## Guiding principles

- Verified, not hoped-for: a successful exit code is not evidence, a read-back
  comparison is.
- Small and boring: one binary, one job per verb, no daemon, GUI or config file.
- Fail loudly and specifically — an unexpected difference is an error, never a
  warning to scroll past.
