"""Pseudo-terminal stand-ins for the RT-4D, for tests without a radio.

Two conversations are modelled, both following the contracts in include/:

``Radio`` is the SPI flash in normal mode. A command in REGIONS erases its whole
span when the index is 0, then programs the 1 KiB chunk at ``base + index *
1024`` (NOR semantics: programming only clears bits, so a write without an erase
ands into what is there). A command outside REGIONS is unknown to the radio and
programs at address 0 without erasing — the failure mode that damages the
calibration sector. A read answers ``52 blkH blkL``, a write is answered with
``06``.

``Bootloader`` is the MCU flash in firmware mode: a probe, two erase frames, then
1 KiB writes whose offset field wraps every 64 KiB, every frame checksummed with
the 0x48 seed.

The region table is a separate copy on purpose: if the tool's table drifts, these
tests fail instead of agreeing with it.
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

# The MCU side: the bootloader's own conversation.
CMD_ERASE_FW = 0x39
CMD_WRITE_FW = 0x57
SEED_FIRMWARE = 0x48
FW_IMAGE_BYTES = 0x3D800
FW_BLOCKS = FW_IMAGE_BYTES // BLOCK
FW_MODE_BYTE = 0xFF
NOT_FIRMWARE_MODE = CMD_READ
ERASE_SETUP = 0x10
ERASE_TRIGGER = 0x55
ERASE_BYTES = 5
ERASE_FIXED = (0x33, 0x05)
OFFSET_MASK = 0xFFFF

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
FIRMWARE_SEED = 7

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


def _firmware_checksum(data):
    return (sum(data) + SEED_FIRMWARE) & BYTE_MASK


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


def firmware_image(seed=FIRMWARE_SEED, size=FW_IMAGE_BYTES):
    """A stand-in firmware image of exactly @p size bytes."""
    rng = random.Random(seed)
    return bytes(rng.getrandbits(8) for _ in range(size))


def addressable(image, calibration=True):
    """A mask of the bytes some region command can write."""
    mask = bytearray(IMAGE_BYTES)
    for command, (base, span) in REGIONS.items():
        if command == CALIBRATION and not calibration:
            continue
        mask[base : base + span] = b"\x01" * span
    return mask


class _Closed(Exception):
    """The peer closed the pty."""


class _Pty:
    """A pty with a serving thread that answers frames; subclasses answer them."""

    def __init__(self):
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

    def _read_exact(self, count):
        buf = b""
        while len(buf) < count and not self._stop.is_set():
            try:
                chunk = os.read(self._master, count - len(buf))
            except OSError as exc:  # the peer closed the pty
                raise _Closed from exc
            if not chunk:
                raise _Closed
            buf += chunk
        if len(buf) != count:
            raise _Closed
        return buf

    def _handle_frame(self):
        raise NotImplementedError

    def _serve(self):
        while not self._stop.is_set():
            readable, _, _ = select.select([self._master], [], [], POLL_SECONDS)
            if not readable:
                continue
            try:
                reply = self._handle_frame()
            except _Closed:
                return
            if reply:
                os.write(self._master, reply)


class Radio(_Pty):
    """The SPI flash in normal mode."""

    def __init__(self, flash, fault=None, fault_at=0, persistent=True):
        super().__init__()
        self.flash = flash
        self.fault = fault
        self.fault_at = fault_at
        self.persistent = persistent
        self.reads = 0
        self.writes = 0
        self.region_writes = 0
        self.unknown = []

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

    def _handle_frame(self):
        first = self._read_exact(1)
        if first[0] == CMD_READ:
            frame = first + self._read_exact(HEADER_BYTES)
            return self._read_reply(_be16(frame[INDEX_HIGH], frame[INDEX_LOW]))
        return self._answer_write(first + self._read_exact(HEADER_BYTES + BLOCK))


class Bootloader(_Pty):
    """The MCU flash in firmware mode: probe, erase, 1 KiB writes."""

    def __init__(self, firmware_mode=True, fault=None, fault_at=0, persistent=True):
        super().__init__()
        self.flash = bytearray([ERASED]) * FW_IMAGE_BYTES
        self.firmware_mode = firmware_mode
        self.fault = fault
        self.fault_at = fault_at
        self.persistent = persistent
        self.frames = 0  # erase and write frames the bootloader was asked to ack
        self.erases = []  # the parts sent, in order
        self.offsets = []  # the offset field of each write, as it arrives
        self.blocks = 0  # blocks programmed, counted by the bootloader itself
        self.bad_checksums = 0

    def _fault_now(self):
        return self.frames >= self.fault_at if self.persistent else self.frames == self.fault_at

    def _answer(self, reply=bytes([ACK])):
        """The answer to an erase or write frame, or None to stay silent."""
        self.frames += 1
        if not self._fault_now():
            return reply
        if self.fault == BAD_ACK:
            return bytes([NOT_ACK])
        if self.fault == SILENT:
            return None
        return reply

    def _probe(self, frame):
        if _firmware_checksum(frame[:3]) != frame[3]:
            self.bad_checksums += 1
        if not self.firmware_mode:
            return bytes([NOT_FIRMWARE_MODE])
        return bytes([FW_MODE_BYTE])

    def _erase(self, frame):
        if _firmware_checksum(frame[:4]) != frame[4]:
            self.bad_checksums += 1
            return bytes([CHECKSUM_ERROR])
        if (frame[1], frame[2]) != ERASE_FIXED:
            self.bad_checksums += 1
            return bytes([CHECKSUM_ERROR])
        self.erases.append(frame[3])
        self.flash = bytearray([ERASED]) * FW_IMAGE_BYTES
        self.blocks = 0
        return self._answer()

    def _write(self, frame):
        if _firmware_checksum(frame[:CHECKSUM_AT]) != frame[CHECKSUM_AT]:
            self.bad_checksums += 1
            return bytes([CHECKSUM_ERROR])
        self.offsets.append(_be16(frame[INDEX_HIGH], frame[INDEX_LOW]))
        address = self.blocks * BLOCK  # the bootloader counts blocks itself
        self.flash[address : address + BLOCK] = frame[PAYLOAD_AT:CHECKSUM_AT]
        self.blocks += 1
        return self._answer()

    def _handle_frame(self):
        first = self._read_exact(1)
        if first[0] == CMD_READ:
            return self._probe(first + self._read_exact(HEADER_BYTES))
        if first[0] == CMD_ERASE_FW:
            return self._erase(first + self._read_exact(ERASE_BYTES - 1))
        return self._write(first + self._read_exact(HEADER_BYTES + BLOCK))
