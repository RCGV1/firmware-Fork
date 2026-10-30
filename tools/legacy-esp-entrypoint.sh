#!/usr/bin/env bash
set -euo pipefail

# Keep the outer core and the ESP-IDF child core compatible with the pinned 55.03.311 platform.
python3 - <<'PY'
from pathlib import Path
import re

source = Path("/tmp/constraints.txt")
text = source.read_text() if source.exists() else ""
text = re.sub(r"(?m)^(?:pioarduino|platformio)[<=>!~].*\n?", "", text)
Path("/tmp/full-position-constraints.txt").write_text(
    text + "\npioarduino==6.1.19\nplatformio==6.1.19\n")
PY
export UV_CONSTRAINT=/tmp/full-position-constraints.txt
export PIP_CONSTRAINT=/tmp/full-position-constraints.txt
uv pip install --system "pioarduino==6.1.19"
if [[ -f "${PLATFORMIO_CORE_DIR}/penv/pyvenv.cfg" ]]; then
    uv pip install --python "${PLATFORMIO_CORE_DIR}/penv/bin/python" "pioarduino==6.1.19"
fi
exec /entrypoint.sh
