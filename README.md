# rt4d-cli

Read, back up and restore the SPI flash of a Radtel RT-4D handheld radio, and
flash its MCU firmware, from the command line.

## Why

Most RT-4D tooling is unmaintained, GUI-only or quietly wrong. The popular
`rt890-flash` computes each write chunk's index from the frame length instead of
the payload, so a restore lands a few bytes late per kilobyte and never writes
the tail of a region. This tool does one job per verb, checks every reply, and
reads back every SPI byte it writes.

## What it does

| Verb         | What it does                                                                                |
| ------------ | ------------------------------------------------------------------------------------------- |
| `--check`    | Opens the port, reports its settings, closes it. Sends nothing to the radio.                |
| `--backup`   | Reads the whole 4 MiB flash into a file, checking every block, and prints the SHA-256.      |
| `--restore`  | Writes a backup back, region by region, then reads every written byte back and compares it. |
| `--firmware` | Writes an MCU firmware image through the radio's bootloader.                                |

## Requirements

- A Radtel RT-4D and a USB serial cable, typically `/dev/ttyUSB0` at 115200 8N1.
- Normal power-on for the SPI verbs; firmware mode for `--firmware`, which is
  power-on with **PTT** held (**MENU** enters the DMR-chip updater instead).
- To build: a C compiler and `make`. Nothing beyond libc and POSIX to run.

## Build

```sh
$ make
$ ./bin/rt4d-cli --help
```

`make test` runs the tests against a modelled radio over a pseudo-terminal — no
hardware, no root, no dump needed. `make fmt-check` and `make tidy` are the
formatting and static-analysis gates.

## Usage

### Check the port

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --check
INFO: opened /dev/ttyUSB0: 115200 baud, 8N1, no flow control, 5000 ms reply deadline
INFO: closed /dev/ttyUSB0
```

### Back up the flash

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --backup -o rt4d-backup.bin
INFO: backing up 4 MiB from /dev/ttyUSB0 to rt4d-backup.bin
INFO: 6% (256/4096 blocks), 9 KiB/s
INFO: 100% (4096/4096 blocks), 9 KiB/s
INFO: 4194304 bytes in 454.2 s (9 KiB/s)
INFO: sha256 30d14afe...
```

`sha256sum rt4d-backup.bin` prints the same digest, and `cmp` compares two
backups byte for byte. A run that fails part way leaves the partial image on
disk and exits non-zero, so the exit code, not the file, says whether it is
complete.

### Restore a backup

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --restore -i rt4d-backup.bin
INFO: restoring rt4d-backup.bin (sha256 30d14afe...) to /dev/ttyUSB0
INFO: writing 2236416 bytes in 2184 chunks; calibration skipped (region 0x40 differs between units)
...
INFO: read back 2236416 bytes in 240.1 s: identical
INFO: 1957888 bytes have no region command and were left alone (12 ranges)
INFO:   0x001000-0x002000     4 KiB
INFO:   0x003000-0x004000     4 KiB
...
```

A backup is linear; a restore is not. The radio takes writes one region at a
time, addressed by the region's command byte and by a chunk index inside it, and
the 19 regions it knows cover a little over half the image. Those bytes are
written and read back; the rest is the radio's own backup banks and live message
records, which no command addresses, so those ranges are left alone and listed at
the end.

Calibration is region `0x40` and differs between units, so it is skipped unless
`--calibration` is given — that is how a radio's own calibration goes back. No
write ever reaches `0x400000` or above, where the runtime Query-ID database
lives.

### Flash firmware

```sh
$ ./bin/rt4d-cli -p /dev/ttyUSB0 --firmware -i rt4d.bin
INFO: flashing rt4d.bin (251904 bytes) over /dev/ttyUSB0
INFO: the radio is in its firmware mode
INFO: MCU flash erased
INFO: writing 246 blocks of 1024 bytes
INFO: 246 blocks acknowledged; MCU flash cannot be read back, so the count is the evidence
```

The image is at most 251,904 bytes and a shorter one is padded with zeros,
because the whole region is always written. MCU flash cannot be read back, so a
flash is checked by what it is — one acknowledgement per block, and a count. An
interrupted run says the MCU may hold a partial image.

## Options

| Flag                | Meaning                                                                       |
| ------------------- | ----------------------------------------------------------------------------- |
| `-p, --port DEVICE` | Serial port the radio is on. Required.                                        |
| `-o, --output FILE` | `--backup` destination, overwritten.                                          |
| `-i, --input FILE`  | `--restore` or `--firmware` source.                                           |
| `--calibration`     | `--restore` also writes region `0x40`.                                        |
| `-t, --timeout SEC` | Reply deadline in seconds, default 5; `s`, `m` and `h` suffixes are accepted. |
| `--baud RATE`       | Line speed, default 115200.                                                   |
| `-v [N]`, `-q [N]`  | More, or less, logging on stderr.                                             |
| `-h, --help`        | Usage text.                                                                   |

## License

Do whatever you want licence. The vendored SHA-256 in `src/sha256.c` is public
domain and keeps its upstream notice.

## Disclaimer

This software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and noninfringement. In no event shall the author be
held liable for any claim, damages or other liability, whether in an action of
contract, tort or otherwise, arising from, out of or in connection with this
software or the use or other dealings in it.

Writing to flash is destructive by nature: a restore, a firmware flash, or a run
interrupted at the wrong moment can leave the radio inoperable until it is
recovered from a backup you already hold. Dump a working radio first, and use
this tool at your own risk.
