#!/usr/bin/env python3
"""Backup tests: drive bin/rt4d-cli --backup against tests/radio.py.

The image comes out of the modelled flash, so every case can be checked byte for
byte against what the radio holds, and against the digest the tool prints.

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

# A block in the middle of the image, so the first-block diagnosis stays untested
# by cases that mean to test the retry.
MIDDLE_BLOCK = 1500
# Where the partial-image case stops: far enough in that the file is non-empty.
STOP_BLOCK = 293


class Backup(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = os.path.join(self.tmp.name, "backup.bin")

    def run_cli(self, port, *args):
        return subprocess.run(
            [BIN, "-p", port, "-t", REPLY_TIMEOUT, *args],
            capture_output=True,
            text=True,
            timeout=CLI_TIMEOUT,
        )

    def backup(self, flash, *extra, out=None, fault=None, fault_at=0, persistent=True):
        with radio.Radio(flash, fault=fault, fault_at=fault_at, persistent=persistent) as model:
            return self.run_cli(model.port, "--backup", "-o", out or self.out, *extra)

    def read(self, path):
        with open(path, "rb") as f:
            return f.read()

    def test_copies_the_whole_flash(self):
        flash = radio.image_with_data()

        result = self.backup(flash)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(digest(self.read(self.out)), digest(flash))
        self.assertIn(f"sha256 {digest(flash)}", result.stderr)
        self.assertIn(f"{radio.IMAGE_BYTES} bytes", result.stderr)

    def test_retries_a_block_that_arrives_corrupt(self):
        flash = radio.image_with_data()

        result = self.backup(
            flash, fault=radio.READ_CHECKSUM, fault_at=MIDDLE_BLOCK, persistent=False
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("checksum", result.stderr)
        self.assertIn("try 1 of 3", result.stderr)
        self.assertEqual(digest(self.read(self.out)), digest(flash))

    def test_reports_a_short_reply_as_a_short_reply(self):
        flash = radio.image_with_data()

        result = self.backup(flash, fault=radio.READ_SHORT, fault_at=MIDDLE_BLOCK, persistent=False)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            f"short read after {radio.SHORT_REPLY_BYTES} of {radio.REPLY_BYTES} bytes",
            result.stderr,
        )
        self.assertEqual(digest(self.read(self.out)), digest(flash))

    def test_keeps_the_blocks_read_before_a_silent_radio(self):
        flash = radio.seeded_flash()

        result = self.backup(flash, fault=radio.READ_SILENT, fault_at=STOP_BLOCK)

        self.assertEqual(result.returncode, 1)
        self.assertIn(f"backup stopped at block {STOP_BLOCK}", result.stderr)
        self.assertIn("partial image", result.stderr)
        partial = self.read(self.out)
        self.assertEqual(len(partial), STOP_BLOCK * radio.BLOCK)
        self.assertEqual(digest(partial), digest(flash[: STOP_BLOCK * radio.BLOCK]))

    def test_names_the_flashing_mode_when_block_0_is_refused(self):
        result = self.backup(radio.seeded_flash(), fault=radio.READ_REFUSED, fault_at=0)

        self.assertEqual(result.returncode, 1)
        self.assertIn("flashing mode", result.stderr)
        self.assertEqual(os.path.getsize(self.out), 0)

    def test_refuses_backup_without_an_output(self):
        with radio.Radio(radio.seeded_flash()) as model:
            result = self.run_cli(model.port, "--backup")

        self.assertEqual(result.returncode, 1)
        self.assertIn("no output file given", result.stderr)

    def test_round_trip_restores_what_it_backed_up(self):
        original = radio.image_with_data()
        self.assertEqual(self.backup(original).returncode, 0)

        second = os.path.join(self.tmp.name, "second.bin")
        with radio.Radio(radio.seeded_flash()) as model:
            restored = self.run_cli(model.port, "--restore", "-i", self.out, "--calibration")
            self.assertEqual(restored.returncode, 0, restored.stderr)
            self.assertEqual(self.backup(model.flash, out=second).returncode, 0)

        mask = radio.addressable(original)
        after = self.read(second)
        at = first_difference(mask, after, original)
        self.assertIsNone(at, "" if at is None else f"first difference at 0x{at:06X}")


if __name__ == "__main__":
    unittest.main()
