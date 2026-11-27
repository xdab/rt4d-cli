# docs/

One technical summary per third-party implementation that talks to the RT-4D:
what it does to the radio, the frames it sends, what it checks, and what it leaves
unproven.

| Document | Project | Language | Covers |
| --- | --- | --- | --- |
| [jcalado-rt4d-cps.md](jcalado-rt4d-cps.md) | jcalado/rt4d-cps | Python | Whole-chip SPI read, per-region write, region table, address book, messages, firmware path |
| [bricky149-rt890-flash-rs.md](bricky149-rt890-flash-rs.md) | bricky149/rt890-flash-rs | Rust | 128-byte dump and restore, range table, the payload divisor the index rule needs |
| [dualtachyon-rt4d-flasher-cli.md](dualtachyon-rt4d-flasher-cli.md) | DualTachyon/radtel-rt-4d-flasher-cli | C# | Smallest MCU conversation: probe, single erase, 1 KiB writes |
| [fagci-rt4d-python-flasher.md](fagci-rt4d-python-flasher.md) | fagci/RT-4D-python-flasher | Python | Reference firmware conversation: two-part erase, 246 blocks |
| [turnrye-openrtx-web-flasher.md](turnrye-openrtx-web-flasher.md) | turnrye/openrtx-web-flasher | TypeScript | Strict reply checks, read-only SPI backup, bootloader simulator |
| [omegatee-rt4d-flasher.md](omegatee-rt4d-flasher.md) | omegatee/RT-4D_Flasher | binary | GUI flasher: image format and boot modes, nothing else readable |

## Status of this directory

Every project summarised here is third-party, upstream and read-only; none of it
is code from this repository or a dependency of it. The documents describe those
projects, not this one — the rules this tool implements live in `include/` once
the transport exists, and that text is the copy of record.

## Where the projects disagree

| Topic | Difference |
| --- | --- |
| Frame shape | Agreed by all six: command byte, two index bytes, payload, one trailing checksum |
| Checksum seed | 0 for SPI frames everywhere; 0x48 for the RT-4D's firmware frames, but a plain sum in bricky149's RT-890 tree |
| Chunk size | 128 bytes in bricky149's published tree; 1024 in every RT-4D tool |
| Erase | Two frames, `39 33 05 10` then `39 33 05 55`, in the Python tools and turnrye's port; a single `39 00 00 55` in the C# and Rust tools |
| Read reply | Three header bytes echo the request: checked by turnrye, ignored by jcalado, never compared by bricky149 |
| Write verification | None of the six reads back what it wrote |

Two questions stay open across the six: what the two erase frames mean, and how
far the writable region ids extend — jcalado uses `0xA4` for the address book
above `0x400000`, and a comment in turnrye's code claims `0x40` or `0x90`–`0xA5`.
