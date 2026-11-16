#!/usr/bin/env python3
"""PlatformIO entry point compatible with pioarduino's 6.2.0.post1 core patch."""
import os
from pathlib import Path
os.environ["PYTHONPATH"] = str(Path(__file__).resolve().parent / "pio_compat") + os.pathsep + os.environ.get("PYTHONPATH", "")
import platformio
# PlatformIO's semver parser treats the PEP 440 .post1 label as a prerelease.
if platformio.__version__.startswith("6.2.0.post"):
    platformio.__version__ = "6.2.0"
from platformio.__main__ import main
main()
