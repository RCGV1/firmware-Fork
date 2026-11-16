#!/usr/bin/env python3
"""Isolated protocol fixture test; requires a configured/built desktop target."""

import json
import pathlib
import shlex
import subprocess

here = pathlib.Path(__file__).resolve().parent
build = here.parent / "build"
entries = json.loads((build / "compile_commands.json").read_text())
base = next(x for x in entries if x["file"].endswith("/DesktopRadio.cpp"))
for source, name in [
    (here.parent / "DesktopRadio.cpp", "fixture-model.o"),
    (here / "fixture_protocol.cpp", "fixture-test.o"),
]:
    args = shlex.split(base["command"])
    args[args.index("-o") + 1] = str(here / name)
    args[args.index("-c") + 1] = str(source)
    args += ["-UNDEBUG", "-I" + str(here.parent)]
    subprocess.run(args, cwd=base["directory"], check=True)
objects = list((build / "CMakeFiles/wadamesh-meshtastic-sim.dir").rglob("*.o"))
selected = [
    str(x)
    for x in objects
    if "/WadaPacketComms/" in str(x)
    or "/Nanopb/" in str(x)
    or ("/generated/meshtastic/" in str(x) and "deviceonly" not in str(x))
    or x.name == "WadaNativeConfig.cpp.o"
]
subprocess.run(
    [
        "/usr/bin/c++",
        str(here / "fixture-test.o"),
        str(here / "fixture-model.o"),
        *selected,
        str(build / "libwada_thirdparty.a"),
        "-o",
        str(here / "fixture-protocol"),
    ],
    check=True,
)
subprocess.run([str(here / "fixture-protocol")], check=True)
