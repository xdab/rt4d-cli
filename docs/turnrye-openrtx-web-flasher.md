# turnrye/openrtx-web-flasher

Browser flasher for OpenRTX radios — TypeScript, Web Serial, GPL-3.0. It carries
the RT-4D as two separate implementations: the MCU bootloader for flashing, and a
read-only path through the stock firmware for SPI backups. The backup sends
nothing but reads, and `src/sim/rt4d.ts` is the only executable model of the
bootloader described here.

Upstream: <https://github.com/turnrye/openrtx-web-flasher>.

## Firmware path (`src/flash/rt4d.ts`)

A port of fagci's Python flasher, cross-checked against fagci's Go version;
packets, checksum, order and the 5 s timeout are taken from there. Constants:
`0x3D800` writable region, `0x400` blocks, load address `0x08002800`, 115200 8N1.

| Operation | Bytes sent | Expected answer |
| --- | --- | --- |
| probe | `52 00 00 9A` | `FF` |
| erase, part 0 | `39 33 05 10 C9` | `06` |
| erase, part 1 | `39 33 05 55 0E` | `06` |
| write chunk | `57 <offset hi> <offset lo> <1024> <sum+0x48>` | `06` |

The checksum is `(sum of the bytes + 72) & 0xFF`, exported as `rt4dChecksum` and
asserted in `src/flash/rt4d.test.ts`. The image is zero-padded, and if its vector
table is present, word 1 (the reset handler) must fall inside
`0x08002800`–`0x08040000` — the load address plus the image — else the tool warns
before flashing. Each read is a single byte with a deadline, so a timeout and a
wrong answer are different errors. The offset field is the low 16 bits of the byte
offset and wraps every 64 KiB; the code says the bootloader keeps count by itself.

## Backup path (`src/backup/rt4d-stock.ts`)

A read-only SPI dump for a radio running stock firmware, built on the block read
from DualTachyon's `rt-4d-spiflash-cli` and the memory map from the `rt-4d-wiki`
Flash-Layout page. It sends nothing but `52` reads and never constructs a region
write.

- Blocks of `0x400`; a dump is the first 1 MiB (`quick`) or 4 MiB (`all`), read in
  order by 16-bit block index.
- Every answer must pass three checks: the three header bytes echo the request, the
  checksum covers header and data, and the length is exact. Failures are
  classified as `silent`, `refused` (the bootloader's `FF`) or `garbled`, and the
  three produce different messages — a `refused` answer is reported as "the radio
  is in flashing mode, where its memory cannot be read".
- Three attempts per block with a 2 s per-piece timeout, stale bytes drained
  between attempts, and an incomplete read returned with a note rather than
  discarded.
- The first 4 KiB (calibration) is fingerprinted with SHA-256 and used as the
  radio's identity; all-`FF` or all-zero calibration means "no identity".
- No `34 00 00 10` notify and no `34 52 05 ee 79` close: the comments note that
  the reference tool itself ignores the notify's reply, that reads need no
  session, and that close only restarts the radio after a write.

Scope notes in that file record two facts the safety rules rest on: the chip is
16 MiB, and the address book above `0x400000` (up to `0xFFFFFF`) is not covered by
any dump.

## What the simulator adds (`src/sim/rt4d.ts`)

`SimRt4d` is a strict bootloader model used by the tests: it splits the byte
stream by command byte (4, 5 and 1028 bytes), requires the probe first, requires
erase parts `0x10` then `0x55` in that order, verifies the `+72` checksum, and only
then accepts 246 writes whose offset field is `(blocks * 1024) & 0xFFFF`. Any
mistake gets a NAK (`0x15`) and everything after it is refused. The header comment
lists exactly what is known and what is invented — probe reply length, NAK value,
and the ordering requirement are inventions — which makes it a usable test double
without pretending to be evidence.

## Protocol facts stated in the comments

- The 3 reply header bytes repeat the request, taken from DualTachyon's RT-890 open
  firmware (`app/uart.c`), which runs the same protocol with 128-byte blocks.
- Region writes, on that same protocol, start with `0x40` or `0x90`–`0xA5`.
- A radio in bootloader mode answers `FF` to a read; that is the mode signal.

## Limits

- Flashing the RT-4D is listed as unverified (`verified: null` in
  `src/radios.ts`), and the boot sequence "hold PTT while powering on" is marked
  `enterUnconfirmed`. The manual fallback it names is fagci's flasher.
- The backup path is read-only by design: no restore, no region writes, no compare.
- The SPI path checks the header echo, the checksum and the length — the strictest
  read validation described here.

## What this project takes from it

The tightest available description of what a good reply check is (echo, checksum,
length), the failure taxonomy silent/refused/garbled, the idea of a first-4-KiB
identity, and a testable bootloader model. Its backup scope notes are the model
for documenting what a dump does and does not cover.
