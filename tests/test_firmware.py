#!/usr/bin/env python3
"""Firmware tests: drive bin/rt4d-cli --firmware against tests/radio.py.

The bootloader side is modelled, so every case checks what the radio was asked
to do: which erase frames arrived, which offsets were sent, which blocks were
programmed, and whether every frame's checksum was accepted.

Run with `make test`, `python3 -m unittest discover -s tests -v`, or this file.
"""

import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import radio
from support import digest

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bin", "rt4d-cli")
CLI_TIMEOUT = 60
REPLY_TIMEOUT = "0.3"  # the model answers at once; keeps failures quick

# Frame counts: the two erase frames come first, so frame N of a flash is a write
# for N > 2. Both faults land well inside the writes.
BAD_ACK_FRAME = 50
SILENT_FRAME = 1  # the first erase, before anything is written


class Firmware(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write_image(self, data, name="firmware.bin"):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def run_cli(self, port, *args):
        return subprocess.run(
            [BIN, "-p", port, "-t", REPLY_TIMEOUT, *args],
            capture_output=True,
            text=True,
            timeout=CLI_TIMEOUT,
        )

    def flash(self, image_path, *extra, **model_args):
        with radio.Bootloader(**model_args) as model:
            result = self.run_cli(model.port, "--firmware", "-i", image_path, *extra)
        return result, model

    def test_flashes_a_whole_image(self):
        image = radio.firmware_image()

        result, model = self.flash(self.write_image(image))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(digest(model.flash), digest(image))
        self.assertEqual(model.erases, [radio.ERASE_SETUP, radio.ERASE_TRIGGER])
        self.assertEqual(model.blocks, radio.FW_BLOCKS)
        self.assertEqual(model.bad_checksums, 0, "a frame's checksum was rejected")
        self.assertIn("acknowledged", result.stderr)

    def test_sends_the_offset_that_wraps_every_64_kib(self):
        result, model = self.flash(self.write_image(radio.firmware_image()))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(model.offsets), radio.FW_BLOCKS)
        self.assertEqual(model.offsets[:3], [0x0000, 0x0400, 0x0800])
        # 64 KiB is 64 blocks, so the offset field returns to zero there
        self.assertEqual(model.offsets[63], 0xFC00)
        self.assertEqual(model.offsets[64], 0x0000)
        self.assertEqual(model.offsets[65], 0x0400)

    def test_pads_a_short_image_with_zeros(self):
        image = radio.firmware_image(size=radio.BLOCK)
        self.assertLess(len(image), radio.FW_IMAGE_BYTES)

        result, model = self.flash(self.write_image(image))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(model.flash[: len(image)], image)
        self.assertEqual(model.flash[len(image) :], bytes(radio.FW_IMAGE_BYTES - len(image)))
        self.assertEqual(model.blocks, radio.FW_BLOCKS)

    def test_refuses_an_image_larger_than_the_region(self):
        oversized = radio.firmware_image(size=radio.FW_IMAGE_BYTES + 1)

        result, model = self.flash(self.write_image(oversized))

        self.assertEqual(result.returncode, 1)
        self.assertIn("larger than", result.stderr)
        self.assertEqual(model.frames, 0, "a frame was sent for a rejected image")

    def test_refuses_a_radio_that_is_not_in_firmware_mode(self):
        result, model = self.flash(self.write_image(radio.firmware_image()), firmware_mode=False)

        self.assertEqual(result.returncode, 1)
        self.assertIn("not in its firmware mode", result.stderr)
        self.assertIn(f"0x{radio.NOT_FIRMWARE_MODE:02X}", result.stderr)
        self.assertEqual(model.erases, [], "the erase was sent to a radio in normal mode")

    def test_stops_when_the_erase_is_not_acknowledged(self):
        result, model = self.flash(
            self.write_image(radio.firmware_image()), fault=radio.SILENT, fault_at=SILENT_FRAME
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("timeout", result.stderr)
        self.assertIn("nothing was written", result.stderr)
        self.assertEqual(model.blocks, 0)

    def test_stops_on_a_bad_ack_during_a_write(self):
        result, model = self.flash(
            self.write_image(radio.firmware_image()), fault=radio.BAD_ACK, fault_at=BAD_ACK_FRAME
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn(f"answered 0x{radio.NOT_ACK:02X}, not 0x{radio.ACK:02X}", result.stderr)
        self.assertIn("partial image", result.stderr)
        self.assertLess(model.blocks, radio.FW_BLOCKS)

    def test_refuses_firmware_without_an_input(self):
        with radio.Bootloader() as model:
            result = self.run_cli(model.port, "--firmware")

        self.assertEqual(result.returncode, 1)
        self.assertIn("no input file", result.stderr)


if __name__ == "__main__":
    unittest.main()
