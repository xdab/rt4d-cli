# bricky149/rt890-flash-rs

Rust CLI dumper and flasher for the Radtel RT-890, Apache-2.0, v1.2.0. Three files
and 381 lines in total, `serialport5` plus `nix` for the root check.

Upstream: <https://github.com/bricky149/rt890-flash-rs>.

## Shape

One `SerialPort` opened per operation, 115200 baud, 8N1. The read timeout is 2 s
for a dump or a firmware write and 3 s for a restore. The tool refuses to run
without effective UID 0, a requirement the upstream README inherits from the
RT-890's cable setup rather than from the protocol.

Verbs are positional and undocumented beyond the usage text: `-l`, `-p PORT -d
FILE`, `-p PORT -f FILE`, `-p PORT -r [-c] FILE`.

## Frames

Fixed 132-byte packet: 3 header bytes, `CHUNK_LENGTH = 128` payload, checksum.

| Operation | Bytes sent | Expected answer |
| --- | --- | --- |
| read SPI block | `52 <block hi> <block lo> <sum>` | 132 bytes: 3 header, 128 data, sum |
| write SPI chunk | `<range cmd> <chunk hi> <chunk lo> <128> <sum>` | `06` |
| erase MCU | `39 00 00 55 <sum>` | `06` |
| write MCU chunk | `57 <offset hi> <offset lo> <128> <sum>` | `06` |

`<sum>` is a plain byte sum, relying on `u8` wrap: the release profile uses
`opt-level = "z"` with LTO and the dev profile disables `overflow-checks`, so the
accumulator wraps rather than panics in both. Note the seed: the RT-4D's
bootloader frames (`39`, `57`) are seeded with `0x48` by every tool that targets
the 4D, while this RT-890 tree sends them with a plain sum. The seed follows the
radio, not the command.

## SPI: dump and restore

`dump_spi_flash` reads block indices 0 upward until the radio answers nothing,
128 bytes at a time, and stops on that `None` — 32768 blocks, 4 MiB, if the radio
keeps answering. Each reply is checksum-verified; on a failed check the block is
read once more, with a comment that the first read after a pause is sometimes
empty, and if that also fails the dump ends there with a partial file. There is no
retry loop beyond that and no hash.

`restore_spi_flash` requires the input file to be exactly `0x400000` bytes and
walks a hard-coded range table in order, in 128-byte chunks:

| Range cmd | Offset | Size |
| --- | --- | --- |
| `0x40` | `0x000000` | 2,949,120 |
| `0x41` | `0x2D0000` | 163,840 |
| `0x42` | `0x2F8000` | 139,264 |
| `0x43` | `0x31A000` | 8,192 |
| `0x47` | `0x3B5000` | 40,960 |
| `0x48` | `0x3BF000` | 4,096 |
| `0x49` | `0x3C1000` | 40,960 |
| `0x4B` | `0x3D8000` | 40,960 |
| `0x4C` | `0x31C000` | 626,688 |

`-c` narrows that to range `0x48`, the 4 KiB calibration area at `0x3BF000`. The
table is the RT-890's and does not transfer: only id `0x40` is shared with the
RT-4D, where it is calibration at offset 0 rather than a 2.8 MiB block.

The one line that matters is the index arithmetic:

```rust
let block_offset = (offset - spi_range.offset) / 128;
```

The divisor is the payload, `CHUNK_LENGTH`, not the frame. The index is therefore
the chunk number counted from the start of the range, which is the rule the RT-4D
needs as well, with 1024 where this tree has 128.

## Firmware

`FIRMWARE_SIZE` is exactly 60,416 bytes and the file must match it. Erase, then
write in 128-byte chunks from offset 0; a single failed ACK panics. Nothing is
read back, and the tool has no idea what region of MCU flash it just wrote beyond
the offset it sent.

## Limits

- The tree targets the RT-890: 128-byte chunks, a 60,416-byte image, `0x40`–`0x4C`
  regions. The source never mentions the RT-4D.
- No read-back, no compare, no dump hash; `dump` output ends wherever the radio
  stopped answering.
- `restore` demands a full 4 MiB file even for the calibration-only path.

## What this project takes from it

The CLI shape, the "dump until the radio stops" loop, and one line of arithmetic
that this project must keep, with the payload as divisor. Its per-range struct is
the right data model for a region table: id, start, size, and nothing inferred.
