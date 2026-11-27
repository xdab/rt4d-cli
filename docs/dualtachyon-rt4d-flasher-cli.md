# DualTachyon/radtel-rt-4d-flasher-cli

C#/.NET console flasher for the RT-4D's MCU firmware, Apache-2.0. Two source
files: `RT-4D-UART.cs` with the wire protocol and `Program.cs` with a `-l` / `-p
COMx -f file.bin` interface. Its README deliberately carries no usage
instructions.

Upstream: <https://github.com/DualTachyon/radtel-rt-4d-flasher-cli>.

## Shape

`System.IO.Ports.SerialPort`, 8N1, 115200 baud, `ReadTimeout` and `WriteTimeout`
100 ms, DTR disabled, no handshake. It opens the port, probes, erases, writes and
closes; there is no other verb and no read path at all — this tool never dumps,
never verifies and never touches the SPI flash. No frame carries a payload longer
than 1024 bytes, and no command is ever answered with more than one byte.

`Program.cs` prints the tool's name and refuses anything but `-l` or
`-p <port> -f <file>`. Opening a port that takes a second or more gets "COM port
seems stuck! Please reinsert the USB cable".

## Session

1. Probe: read bytes; whenever a read times out, write `FF`. Any `FF` read back
   ends the probe, and any other byte is noted once and skipped.
2. Erase: `39 00 00 55 <sum>`, answered with `06`; retried up to 400 times at
   100 ms each (about 40 s).
3. Write: 1024-byte chunks for the length of the file, each `06` acknowledged
   with up to 100 retries (about 10 s).

The probe echoes `FF`; it does not use the `52` read or the `34` notify that the
RT-4D firmware answers. No handshake precedes the erase.

## Frames

| Operation | Bytes sent | Expected answer |
| --- | --- | --- |
| erase MCU | `39 00 00 55 <sum+0x48>` | `06` |
| write MCU chunk | `57 <offset hi> <offset lo> <1024> <sum+0x48>` | `06` |

Every frame is seeded with `0x48`, the same value the other RT-4D tools use for
bootloader frames; this tool has no zero-seeded frame at all.

The write frame is always 1028 bytes: the last chunk of a file whose length is not
a multiple of 1024 is zero-filled to the full payload, and the checksum covers
those zero bytes. The offset field is the low 16 bits of the byte offset, so it
wraps every 64 KiB and the bootloader counts blocks itself.

## Limits

- Firmware only: no SPI read or write, no region table, no dump, no compare.
- One erase frame, `0x55` only. The tools described in
  `fagci-rt4d-python-flasher.md` and `jcalado-rt4d-cps.md` send two erase frames,
  `39 33 05 10` then `39 33 05 55`. Both sequences are in circulation and nothing
  here establishes what separates the two halves.
- No image-size or vector-table check: any file is flashed, and a short file
  simply ends the loop.
- Errors are `Console.WriteLine` plus `false`; a timeout and a wrong byte are the
  same outcome after the retry budget.

## What this project takes from it

The probe-then-erase-then-write order, the 1 KiB frame with the low 16 bits of the
offset, the `0x48`-seeded checksum, and the retry budgets as a starting point. Its
`RT4D_UART` class and `Command_*` methods are the names jcalado's Python module
cites as its C# source, which makes this the smallest readable statement of the
4D bootloader conversation.
