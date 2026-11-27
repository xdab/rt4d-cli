# tests/

The tool's tests. They drive `bin/rt4d-cli` over a pseudo-terminal whose other
end is a model of the radio's SPI flash, so the frames are real and the radio is
not.

| Path | Contents |
| --- | --- |
| [radio.py](radio.py) | The models: `Radio` for the SPI flash (contents, erase and program semantics, the region table) and `Bootloader` for the MCU (probe, erase frames, offset wrap), plus the faults a test can inject on either side. |
| [support.py](support.py) | Two helpers the test modules share: a digest, and the first address where two images disagree. |
| [test_backup.py](test_backup.py) | `--backup` cases: the whole flash, a block that arrives corrupt, a short reply, silence part way, a refusal on block 0, a missing `-o`, and a backup → restore → backup round trip. |
| [test_restore.py](test_restore.py) | `--restore` cases: whole-image write, calibration opt-in, idempotence, bad ACK, a write that lands wrong, silence on the line, argument handling. |
| [test_firmware.py](test_firmware.py) | `--firmware` cases: a whole image, the wrapping offset field, zero padding, an oversized image, a radio not in firmware mode, a silent erase, a bad ACK mid-write, a missing `-i`. |

## Running

```sh
make test                     # or: python3 -m unittest discover -s tests -v
```

No serial device, no root and no recorded dump are involved: the image is
generated and the flash is modelled.

Formatting and lint for these files are `ruff.toml` and `make fmt-check`; both
languages share the 100-column budget.

## What the model pins down

`radio.py` keeps its own copy of the region table, on purpose. If the table in
`src/spi.c` drifts, these tests fail instead of agreeing with it. The model also
reproduces the rules that make a restore safe or destructive: writing index 0
erases a region's whole span, and a command outside the table programs at
address 0 without erasing. A case fails if the tool ever sends one of those.

A fault is named by the unit it hits — a block for reads, a write for writes —
and is persistent by default; pass `persistent=False` for a single bad answer,
which is what the retry lives for.
