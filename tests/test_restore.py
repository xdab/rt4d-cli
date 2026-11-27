#!/usr/bin/env python3
"""Restore tests: drive bin/rt4d-cli --restore against tests/radio.py.

No radio and no recorded dump are needed: the image is generated, the flash is
modelled, and the pty carries the frames. Every test asserts on the tool's exit
status, its message, and the modelled flash afterwards.

Run with `make test`, `python3 -m unittest discover -s tests -v`, or this file.
"""

import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import radio
from support import digest, first_difference

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bin", "rt4d-cli")
CLI_TIMEOUT = 60
REPLY_TIMEOUT = "0.3"  # the model answers at once; keeps failures quick

CHUNKS_ALL = sum(span for _, span in radio.REGIONS.values()) // radio.BLOCK
CALIBRATION_BYTES = radio.REGIONS[radio.CALIBRATION][1]
CALIBRATION_CHUNKS = CALIBRATION_BYTES // radio.BLOCK

# A file that is a chunk, not a dump: enough to be refused for its size.
NOT_A_DUMP_BYTES = radio.BLOCK

# Faults land in the middle of a region, never on its first chunk: the first
# chunk erases the whole span and the tool waits up to a minute for that answer,
# which would slow the suite down without testing anything more.
FAULT_BAD_ACK_AT = 500  # inside region 0x9C
FAULT_CORRUPT_AT = 900  # inside region 0xA1
FAULT_SILENT_AT = 300  # inside region 0x94


class Restore(unittest.TestCase):
    def setUp(self):
        self.image = radio.image_with_data()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.image_path = os.path.join(self.tmp.name, "image.bin")
        with open(self.image_path, "wb") as f:
            f.write(self.image)

    def run_cli(self, port, *args):
        return subprocess.run(
            [BIN, "-p", port, "-t", REPLY_TIMEOUT, *args],
            capture_output=True,
            text=True,
            timeout=60,
        )

    def restore(self, flash, *extra, fault=None, fault_at=0):
        with radio.Radio(flash, fault=fault, fault_at=fault_at) as model:
            result = self.run_cli(model.port, "--restore", "-i", self.image_path, *extra)
        return result, model

    def test_writes_every_addressable_byte(self):
        mask = radio.addressable(self.image)
        original = radio.seeded_flash()
        gaps = digest(original[i] for i in range(radio.IMAGE_BYTES) if not mask[i])

        result, model = self.restore(original, "--calibration")

        self.assertEqual(result.returncode, 0, result.stderr)
        at = first_difference(mask, model.flash, self.image)
        self.assertIsNone(at, "" if at is None else f"first difference at 0x{at:06X}")
        self.assertEqual(
            digest(model.flash[i] for i in range(radio.IMAGE_BYTES) if not mask[i]),
            gaps,
            "a byte no command addresses was changed",
        )
        self.assertEqual(model.region_writes, CHUNKS_ALL)
        self.assertEqual(model.unknown, [], "a command outside the table was sent")

    def test_skips_calibration_by_default(self):
        original = radio.seeded_flash()
        mask = radio.addressable(self.image, calibration=False)

        result, model = self.restore(original)

        self.assertEqual(result.returncode, 0, result.stderr)
        at = first_difference(mask, model.flash, self.image)
        self.assertIsNone(at, "" if at is None else f"first difference at 0x{at:06X}")
        self.assertEqual(
            bytes(model.flash[:0x1000]),
            bytes(original[:0x1000]),
            "region 0x40 was written without --calibration",
        )
        self.assertEqual(model.region_writes, CHUNKS_ALL - 4)

    def test_is_idempotent(self):
        result, model = self.restore(bytearray(self.image), "--calibration")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("already holds this image", result.stderr)
        self.assertEqual(digest(model.flash), digest(self.image))

    def test_stops_on_a_bad_ack(self):
        result, _ = self.restore(
            bytearray(self.image), "--calibration", fault="badack", fault_at=500
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("answered 0x00, not 0x06", result.stderr)
        self.assertIn("part-written", result.stderr)

    def test_catches_a_write_that_lands_wrong(self):
        result, _ = self.restore(
            bytearray(self.image), "--calibration", fault="corrupt", fault_at=900
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("read-back differs at 0x", result.stderr)

    def test_reports_a_silent_radio(self):
        result, _ = self.restore(
            bytearray(self.image), "--calibration", fault="silent", fault_at=300
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("timeout", result.stderr)

    def test_refuses_bad_arguments(self):
        small = os.path.join(self.tmp.name, "small.bin")
        with open(small, "wb") as f:
            f.write(bytes(NOT_A_DUMP_BYTES))

        with radio.Radio(radio.seeded_flash()) as model:
            cases = [
                (("--restore",), "no input file"),
                (("--restore", "-i", small), "not a 4 MiB image"),
                (("--restore", "-i", os.path.join(self.tmp.name, "absent.bin")), "cannot open"),
                (
                    ("--backup", "-o", os.path.join(self.tmp.name, "x.bin"), "-i", self.image_path),
                    "input is only used by --restore",
                ),
            ]
            for args, message in cases:
                with self.subTest(args=args):
                    result = self.run_cli(model.port, *args)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
