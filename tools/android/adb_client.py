"""Shared adb helpers for the personal Android probes (explicit serial only)."""
from __future__ import annotations

from collections.abc import Callable
import subprocess

PACKAGE = 'com.nomadic.sc64.personal'
# The launcher APK without game code (ADR-089) has its own application ID and data.
LAUNCHER_PACKAGE = 'com.nomadic.sc64.launcher'
ACTIVITY_CLASS = 'com.nomadic.sc64.Sc64Activity'
ADB_TIMEOUT = 60
# install -r / pull move the whole APK; a bundled-ROM build exceeds 32 MiB.
ADB_TRANSFER_TIMEOUT = 600


def device_command(adb: str, serial: str) -> Callable[..., str]:
    """Return ``run(*args, timeout=ADB_TIMEOUT) -> stripped stdout`` for one device."""
    def run(*command: str, timeout: float = ADB_TIMEOUT) -> str:
        return subprocess.check_output([adb, '-s', serial, *command], text=True, timeout=timeout).strip()
    return run


def parse_pids(text: str) -> tuple[int, ...]:
    """Parse ``pidof`` output, which lists every matching process."""
    try:
        return tuple(int(token) for token in text.split())
    except ValueError as error:
        raise ValueError(f'Unexpected pidof output: {text!r}') from error


def single_pid(text: str, package: str = PACKAGE) -> int | None:
    """Return the only PID in ``pidof`` output, None if empty; reject several."""
    pids = parse_pids(text)
    if len(pids) > 1:
        raise ValueError(f'Several {package} processes are running ({text}); '
                         'force-stop the app and retry so the log belongs to one process')
    return pids[0] if pids else None
