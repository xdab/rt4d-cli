# AGENTS.md

`README.md` is the human front page; this one is for agents.

## State

The read path is real: `uart.c` opens a raw 115200 8N1 port and moves exact byte
counts under a deadline, `frame.c` builds the `52` block read and checks its
reply, `spi.c` walks the 4 MiB image with three attempts per block, and
`main.c` parses options and runs `--check` or `--backup`. `logging.c` is the
stderr logger; `sha256.c` is Brad Conte's public-domain SHA-256, vendored whole
with its notice. Nothing writes yet: `--restore`, the region table and the
firmware path are still to come. `tests/` is still empty; the read path was
exercised on a pseudo-terminal with a scripted radio, which is not a substitute
for the recorded-frame tests of milestone 1.

## Layout

| Path | Holds |
|------|-------|
| `src/` | C sources, one module per file pair: `main.c` (verb dispatch), `options.c`, `uart.c`, `frame.c`, `spi.c`, `logging.c`, `sha256.c` (vendored) |
| `include/` | the matching `X.h`, with the project's documentation |
| `tests/` | unit tests over recorded frames and image files, no radio needed (planned) |
| `tools/` | helper scripts around the binary — fixture capture, analysis (planned) |
| `docs/` | one summary per third-party implementation, plus its `README.md` index |
| `obj/`, `bin/` | build output, gitignored |

One binary, `bin/rt4d-cli`. Still to come: `spi.c`'s region table and restore
half, `firmware.c` (MCU erase and block writes), `tests/`.

## Build and gates

`make` compiles `src/*.c` under `-std=c11 -Wall -Wextra -Wpedantic` into
`bin/rt4d-cli`; `make help` lists the rest. Style is fixed by `.clang-format`
(Allman, 4 spaces, 100 columns) and `.clang-tidy` (bugprone, cert, misc and a
few readability checks).

Before a commit: a clean `make`, `make fmt-check` and `make tidy`. `make test`
arrives with milestone 1. No gate builds or touches the radio.

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
