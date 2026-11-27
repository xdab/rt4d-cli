# fagci/RT-4D-python-flasher

One 3 KB Python file, GPL-3.0, that flashes RT-4D MCU firmware over pySerial. It
does one thing and does it completely: probe, two-part erase, 246 ordered 1 KiB
writes.

Upstream: <https://github.com/fagci/RT-4D-python-flasher>.

## Shape

The flasher subclasses `serial.Serial` (8N1, 115200) and exposes the protocol as
six methods. The CLI takes exactly two arguments, a port and a firmware file;
there are no flags. The read timeout is 5 s, and `write_timeout=5000` — pyserial
reads that as seconds, so writes are effectively unbounded.

## Frames

| Operation | Bytes sent | Expected answer |
| --- | --- | --- |
| probe | `52 00 00 9A` | `FF` (first byte of the reply) |
| erase, part 0 | `39 33 05 10 C9` | `06` |
| erase, part 1 | `39 33 05 55 0E` | `06` |
| write chunk | `57 <offset hi> <offset lo> <1024> <sum+0x48>` | `06` |

`append_checksum` adds 72 (`0x48`) to the byte sum and takes the low 8 bits; that
seed applies to all four frames, probe included. The probe is a read request for
block 0: a radio in bootloader mode answers `FF` instead of a block, which is how
the tool tells the two modes apart. Before probing it drains whatever is already
waiting on the port.

## Session

1. Drain, then probe; a first byte other than `FF`, or silence, ends the run with
   "radio not on flashing mode".
2. Erase part 0, then erase part 1 — two separate commands, both acknowledged.
   Neither is retried.
3. Zero-pad the image to `0x3D800` (251,904 bytes) if it is shorter, and write
   all 246 blocks of `0x400` bytes in order, acknowledging each.
4. Report the bytes written and fail unless the total is exactly `0x3D800`.

The write frame carries the low 16 bits of the byte offset, wrapping every 64 KiB;
the bootloader tracks the block count. The 1 KiB block is the whole payload —
there is no partial write and no trailing short block, because the image is always
padded to a multiple of the block size.

## Limits

- Firmware only: no SPI path, no dump, no backup, no compare.
- No retries anywhere; a dropped ACK aborts mid-image.
- The image is checked by arithmetic only — the byte count of what was written.
  Nothing is read back, and the probe establishes that a bootloader is answering,
  not what it holds.
- The whole `0x3D800` region is always written, so an image cannot leave its tail
  untouched.
- The two-part erase appears here and in the Python and TypeScript tools built on
  this one; the C# tool sends the `0x55` half alone, and nothing described here
  explains what separates the halves.

## What this project takes from it

The exact bootloader conversation — probe, two-part erase, ordered 1 KiB writes,
`0x48` seed, low-16-bit offset field — and the sizes: `0x3D800` region, `0x400`
blocks, 246 of them. Anything this project sends above `0x08002800 + 0x3D800`
would be new ground.
