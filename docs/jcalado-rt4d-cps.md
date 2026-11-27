# jcalado/rt4d-cps

Python codeplug editor for the RT-4D. Its UART module (`rt4d_uart.py`) is the most
complete host-side SPI implementation described here: a whole-chip read,
per-region writes, the address book, messages and the MCU firmware path all live
in one class.

Upstream: <https://github.com/jcalado/rt4d-cps>. No licence file.

## Transport

pyserial, 8N1, 115200 baud by default, read timeout 10 s, write timeout 2 s.
`open_with_fallback()` exists for a 256000 mode: it opens at the requested rate,
tests the link with a notify, and drops to 115200 if the notify goes unanswered;
its own message says high speed needs `#` held while the radio boots. Every
operation flushes after writing and then reads a fixed number of bytes.

## Frames

| Operation | Bytes sent | Expected answer |
| --- | --- | --- |
| notify | `34 00 00 10 44` | `06` |
| close | `34 52 05 EE 79` | none read |
| read SPI block | `52 00 <block hi> <block lo> <sum>` | `<3-byte header> <1024> <sum>` |
| write SPI chunk | `<region> <chunk hi> <chunk lo> <1024> <sum>` | `06` |
| write firmware chunk | `57 <offset hi> <offset lo> <1024> <sum+0x48>` | `06` |

`44` and `79` are the sums of everything before them; the fixed frames are
asserted byte-for-byte in the test suite. Unlike the firmware frames, every SPI
frame is checksummed with a plain byte sum, no seed.

The read path reads exactly 1028 bytes per block, treats header byte `FF` as an
error, verifies the checksum, and returns the 1024 payload bytes. It does not
check that the 3 header bytes repeat the request. `read_spi_region(address, size)`
builds on it, reading from a block boundary and slicing to the requested bytes, so
a region read is one block read per KiB even when the start is unaligned.

The write path takes the index from the payload count: `command_write_spi` loops
`i` over the data in 1024-byte steps and sends `i // 1024` as the index, with the
region id in byte 0. That is the chunk-relative rule; the region start never
enters the frame.

## Region map

`rt4d_codeplug/constants.py` carries the twelve regions the CPS needs, keyed by
name and holding id, address and size:

| Id | Name | Address | Size |
| --- | --- | --- | --- |
| `0x40` | calibration | `0x000000` | 4096 |
| `0x90` | main_settings | `0x002000` | 4096 |
| `0x91` | channels | `0x004000` | 49152 |
| `0x92` | zones | `0x01C000` | 131072 |
| `0x93` | contacts | `0x05C000` | 65536 |
| `0x94` | groups | `0x07C000` | 12288 |
| `0x95` | dmr_keys | `0x082000` | 12288 |
| `0x96` | call_log | `0x088000` | 49152 |
| `0x97` | default_sms | `0x094000` | 4096 |
| `0x98` | schedules | `0x0C6000` | 8192 |
| `0x99` | fm_settings | `0x0D6000` | 4096 |
| `0x80` | dtmf_names | `0x0C7000` | 256 |

`0x9A` and above — startup logo, UTF-16 map, phonetics, fonts, voice prompts —
are not in the table, so the region-write helper cannot reach them; the address
book is the one region above `0x9A` this tool writes, through its own method.
`dtmf_names` sits inside the schedules range and is written under its own id.

Two further maps are worth knowing:

- **Codeplug file layout.** The editor's `.4rdmf` file is a packed 275,712-byte
  image (275,456 without DTMF names) whose sections are at different offsets than
  the SPI regions: channels at `0x1000` in the file, `0x4000` in flash. Region
  reads and writes are translated per section, never by copying a file offset
  straight into a frame index.
- **Settings banks.** Bank 0 at `0x002000` and bank 1 at `0x003000`, each with a
  `DTCN` magic at its `+0xFFC`; the zone regions get an A/B marker
  (`FF FF FF FF DTCN`) at `+0xFF8` of each 4 KiB page, used to pick the live bank.

## Address book and messages

`command_write_addressbook` writes the global contact database with region id
`0xA4`, a 4-byte big-endian length prefix, 1 KiB blocks padded with `FF`, and a
28 MiB (29,360,124-byte) ceiling. It is the only path in the tool that writes
above `0x400000`, and it distinguishes two failure bytes: `0xA4` (flash capacity
mismatch) and `0x4A` (capacity limit reached). Nothing reads it back.

Messages are addressed by a separate table — presets at `0x094000`, drafts at
`0x095000`, inbox at `0x0A5000`, outbox at `0x0B5000`, 256 entries of 256 bytes
each. Reads use `0x52`. Writes reuse region id `0x97` for all four areas, with a
comment that the real ids may differ. No region table described here covers
`0x095000`–`0x0C5000`, so that area is the weakest corner of the protocol
knowledge described here.

## Firmware

`0x3D800` bytes (251,904), zero-padded, in 1 KiB chunks. The session is:

1. probe: send `FF` up to 50 times until the answer is `FF`;
2. handshake: `39 33 05 10` three times, ACK after each, 10 retries at 50 ms;
3. erase trigger: `39 33 05 55`, ACK, 10 retries;
4. writes: `57` with the low 16 bits of the byte offset, 100 retries at 50 ms.

The handshake and the erase trigger are the same frame with `0x10` replaced by
`0x55`; the offset field wraps every 64 KiB and the bootloader keeps its own
count. Each chunk is fully written before the next, and the code raises rather
than retrying past the cap.

## Limits

- The test suite drives a serial double (`FakeSerial`) and pins the fixed frames,
  the read path, full-block writes, NAK handling and the address book's framing
  and size limit; the firmware path has no test.
- Writes are checked by ACK only: no read-back, no compare, no dump verification.
- Failures are `print` + `False`; a timeout and a wrong answer are the same
  outcome.
- The address book and message paths are the only implementations of those areas
  described here, and the ones with the least evidence behind their region ids.

## What this project takes from it

The framing and checksum rules, the block-relative read and chunk-relative write
split, the region table as data, and the shape of a session (notify, read, write,
close). The recorded-frame unit tests are the model to copy for the transport.
