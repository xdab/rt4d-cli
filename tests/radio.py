"""A pseudo-terminal stand-in for the RT-4D's SPI flash, for tests without a radio.

The model follows the write contract in include/spi.h:

* a command in REGIONS erases its whole span when the index is 0, then programs
  the 1 KiB chunk at ``base + index * 1024`` (NOR semantics: programming only
  clears bits, so a write without an erase ands into what is there);
* a command outside REGIONS is unknown to the radio and programs at address 0
  without erasing — the failure mode that damages the calibration sector;
* a read answers ``52 blkH blkL`` with three header bytes, 1024 data bytes and a
  checksum;
* a write is answered with ``06``.

The table is a separate copy on purpose: if the tool's table drifts, these tests
fail instead of agreeing with it.
"""

import os
import pty
import random
import select
import threading

BLOCK = 1024
IMAGE_BYTES = 4 * 1024 * 1024

# Wire values.
CMD_READ = 0x52
ACK = 0x06
ERASED = 0xFF
BYTE_MASK = 0xFF

# Frame layout: [command][index hi][index lo][1024 data][checksum].
CMD_BYTE = 0
INDEX_HIGH = 1
INDEX_LOW = 2
HEADER_BYTES = 3
PAYLOAD_AT = HEADER_BYTES
CHECKSUM_AT = HEADER_BYTES + BLOCK
REPLY_BYTES = CHECKSUM_AT + 1

# The region whose bytes differ between units.
CALIBRATION = 0x40

# Faults a test can inject, and the answers that go with them.
BAD_ACK = "badack"
CORRUPT = "corrupt"
SILENT = "silent"
NOT_ACK = 0x00
CHECKSUM_ERROR = 0x15

# Read-side faults: a reply that arrives corrupt, stops mid-frame, is refused, or
# never arrives.
READ_CHECKSUM = "read-checksum"
READ_SHORT = "read-short"
READ_REFUSED = "read-refused"
READ_SILENT = "read-silent"
BOOTLOADER = 0xFF  # first reply byte of a radio in its flashing mode
SHORT_REPLY_BYTES = 100

# Timing, in seconds: how long the serving thread sleeps between polls and how
# long teardown waits for it to notice.
POLL_SECONDS = 0.1
JOIN_SECONDS = 2.0

# Seeds for the two fixtures, so every run models the same bytes.
NOISE_SEED = 1234
IMAGE_SEED = 99

# command byte -> (first address it writes, span its erase covers)
REGIONS = {
    0x40: (0x000000, 0x001000),
    0x90: (0x002000, 0x001000),
    0x91: (0x004000, 0x00C000),
    0x92: (0x01C000, 0x001000),
    0x93: (0x01E000, 0x020000),
    0x94: (0x05E000, 0x034000),
    0x95: (0x0C6000, 0x005000),
    0x96: (0x0D0000, 0x003000),
    0x97: (0x0D6000, 0x001000),
    0x98: (0x0F0000, 0x001000),
    0x9A: (0x100000, 0x001000),
    0x9C: (0x14C000, 0x018000),
    0x9D: (0x164000, 0x024000),
    0x9E: (0x188000, 0x010000),
    0x9F: (0x198000, 0x004000),
    0xA0: (0x19C000, 0x002000),
    0xA1: (0x19E000, 0x0B4000),
    0xA3: (0x352000, 0x09E000),
    0xA5: (0x3F0000, 0x010000),
}


def _checksum(data):
    return sum(data) & BYTE_MASK


def _be16(high, low):
    """The 16-bit big-endian value of two bytes: a block or chunk index."""
    return (high << 8) | low


def seeded_flash(seed=NOISE_SEED):
    """A whole flash of noise: every address differs from a fresh dump."""
    rng = random.Random(seed)
    return bytearray(rng.getrandbits(8) for _ in range(IMAGE_BYTES))


def erased_flash():
    return bytearray([ERASED]) * IMAGE_BYTES


def image_with_data(seed=IMAGE_SEED):
    """A stand-in dump: noise inside the addressable regions, erased elsewhere."""
    rng = random.Random(seed)
    image = erased_flash()
    for base, span in REGIONS.values():
        image[base : base + span] = bytes(rng.getrandbits(8) for _ in range(span))
    return bytes(image)


def addressable(image, calibration=True):
    """A mask of the bytes some region command can write."""
    mask = bytearray(IMAGE_BYTES)
    for command, (base, span) in REGIONS.items():
        if command == CALIBRATION and not calibration:
            continue
        mask[base : base + span] = b"\x01" * span
    return mask


class Radio:
    """A pty whose other end behaves like the radio's flash."""

    def __init__(self, flash, fault=None, fault_at=0, persistent=True):
        self.flash = flash
        self.fault = fault
        self.fault_at = fault_at
        self.persistent = persistent
        self.reads = 0
        self.writes = 0
        self.region_writes = 0
        self.unknown = []
        self._stop = threading.Event()
        self._master, self._slave = pty.openpty()
        self.port = os.ttyname(self._slave)
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *_):
        self._stop.set()
        for fd in (self._master, self._slave):
            try:
                os.close(fd)  # unblocks the serving thread at once
            except OSError:
                pass
        self._thread.join(timeout=JOIN_SECONDS)

    def addressable_bytes(self, calibration=True):
        return addressable(self.flash, calibration)

    def _read_exact(self, count):
        buf = b""
        while len(buf) < count and not self._stop.is_set():
            try:
                chunk = os.read(self._master, count - len(buf))
            except OSError:  # the peer closed the pty
                return None
            if not chunk:
                return None
            buf += chunk
        return buf if len(buf) == count else None

    def _fault_now(self, count):
        return count >= self.fault_at if self.persistent else count == self.fault_at

    def _read_reply(self, block):
        self.reads += 1
        payload = bytes(self.flash[block * BLOCK : (block + 1) * BLOCK])
        header = bytes([CMD_READ]) + bytes([(block >> 8) & BYTE_MASK, block & BYTE_MASK])
        reply = header + payload + bytes([_checksum(header + payload)])
        # a read fault is named by the block it hits, so counts start at block 0
        if not self._fault_now(self.reads - 1):
            return reply
        if self.fault == READ_CHECKSUM:
            return reply[:-1] + bytes([(reply[-1] + 1) & BYTE_MASK])
        if self.fault == READ_SHORT:
            return reply[:SHORT_REPLY_BYTES]
        if self.fault == READ_REFUSED:
            return bytes([BOOTLOADER])
        if self.fault == READ_SILENT:
            return None
        return reply

    def _answer_write(self, frame):
        self.writes += 1
        command = frame[CMD_BYTE]
        index = _be16(frame[INDEX_HIGH], frame[INDEX_LOW])
        data = frame[PAYLOAD_AT:CHECKSUM_AT]
        if _checksum(frame[:CHECKSUM_AT]) != frame[CHECKSUM_AT]:
            return bytes([CHECKSUM_ERROR])
        if command in REGIONS:
            base, span = REGIONS[command]
            if index == 0:
                self.flash[base : base + span] = bytes([ERASED]) * span
            address = base + index * BLOCK
        else:
            self.unknown.append((command, index))
            address = index * BLOCK
        for offset, value in enumerate(data):
            self.flash[address + offset] &= value
        self.region_writes += 1
        if self.fault == CORRUPT and self.writes == self.fault_at:
            self.flash[address] ^= BYTE_MASK
        elif self._fault_now(self.writes):
            if self.fault == BAD_ACK:
                return bytes([NOT_ACK])
            if self.fault == SILENT:
                return None
        return bytes([ACK])

    def _serve(self):
        while not self._stop.is_set():
            readable, _, _ = select.select([self._master], [], [], POLL_SECONDS)
            if not readable:
                continue
            first = self._read_exact(1)
            if not first:
                return
            if first[0] == CMD_READ:
                rest = self._read_exact(HEADER_BYTES)
                if not rest:
                    return
                frame = first + rest
                reply = self._read_reply(_be16(frame[INDEX_HIGH], frame[INDEX_LOW]))
            else:
                rest = self._read_exact(HEADER_BYTES + BLOCK)
                if not rest:
                    return
                reply = self._answer_write(first + rest)
            if reply is not None:
                os.write(self._master, reply)
