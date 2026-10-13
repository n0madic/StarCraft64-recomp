"""Stage 0 acceptance profile of the resident libultra inventory.

One place for the numbers that tell a regenerated inventory apart from a wrong one:
`ensure_inventory.py` validates against them before publishing and
`tools/research/check_acceptance.py` rechecks them.
"""

from __future__ import annotations

# Resident segment: ROM [0x1000, 0xD6640) is loaded at RAM = ROM + 0x7FFFF400.
RESIDENT_SCAN_START = 0x1000
RESIDENT_SCAN_END = 0xD6640
RESIDENT_DISPLACEMENT = 0x7FFFF400
REFERENCE_VERSIONS = ('I', 'J', 'K', 'L')
ACCEPTED_REFERENCE_MATCHES = 346
ACCEPTED_CRC_HITS = 121
ACCEPTED_REPLACEMENT_NAMES = 51


def replacement_names(inventory: dict) -> set[str]:
    return {row['name'] for row in inventory['inventory']
            if row['reimplemented'] and (row['crc_bodies'] or row['reference_bytes'])}


def validate_resident(inventory: dict, rom_sha1: str) -> None:
    """Raise ValueError unless ``inventory`` is the accepted resident-section result."""
    if inventory.get('rom_sha1') != rom_sha1:
        raise ValueError('inventory belongs to a different ROM')
    mapping = inventory['mapping']
    if (mapping['scan_start'], mapping['scan_end'], mapping['displacement']) != (
            RESIDENT_SCAN_START, RESIDENT_SCAN_END, RESIDENT_DISPLACEMENT):
        raise ValueError('inventory is not the resident-section scan')
    if len(inventory['reference_matches']) != ACCEPTED_REFERENCE_MATCHES:
        raise ValueError(f"{len(inventory['reference_matches'])} reference matches, expected {ACCEPTED_REFERENCE_MATCHES}")
    if len(inventory['masked_crc_hits']) != ACCEPTED_CRC_HITS:
        raise ValueError(f"{len(inventory['masked_crc_hits'])} masked CRC hits, expected {ACCEPTED_CRC_HITS}")
    if not all(row['ram_bytes_equal'] for row in inventory['reference_matches'] + inventory['masked_crc_hits']):
        raise ValueError('a matched body differs from the captured loaded RAM (or no RAM dump was given)')
    if len(replacement_names(inventory)) != ACCEPTED_REPLACEMENT_NAMES:
        raise ValueError(f'{len(replacement_names(inventory))} replacement names, expected {ACCEPTED_REPLACEMENT_NAMES}')
