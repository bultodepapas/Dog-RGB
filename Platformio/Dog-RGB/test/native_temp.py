"""Owned native-build directory with bounded Windows file-lock cleanup retries."""
from contextlib import contextmanager
import os
import tempfile
import time


@contextmanager
def native_temp_dir(prefix=None):
    directory = tempfile.TemporaryDirectory(prefix=prefix)
    try:
        yield directory.name
    finally:
        for attempt in range(11):
            try:
                directory.cleanup()
                break
            except OSError as error:
                # Win32 may briefly retain an executable after subprocess exits.
                # tempfile can wrap sharing violation 32 as NotADirectory 267.
                # Never ignore persistent locks or unrelated filesystem failures.
                if os.name != "nt" or getattr(error, "winerror", None) not in (32, 267) or attempt == 10:
                    raise
                time.sleep(0.1)
