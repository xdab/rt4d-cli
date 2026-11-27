"""Helpers shared by the test modules."""

import hashlib


def digest(data):
    return hashlib.sha256(bytes(data)).hexdigest()


def first_difference(mask, got, want):
    """The first address @p mask covers where @p got and @p want disagree."""
    for address, present in enumerate(mask):
        if present and got[address] != want[address]:
            return address
    return None
