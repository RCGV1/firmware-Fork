import json
import shlex
import subprocess
from pathlib import Path

here = Path(__file__).resolve().parent
build = here.parent / "build"
entry = next(
    x
    for x in json.loads((build / "compile_commands.json").read_text())
    if x["file"].endswith("/DesktopKeyboard.cpp")
)
objects = []
for source, name in [
    (here.parent / "DesktopKeyboard.cpp", "keyboard-model.o"),
    (here / "keyboard_input.cpp", "keyboard-test.o"),
]:
    args = shlex.split(entry["command"])
    obj = here / name
    objects.append(str(obj))
    args[args.index("-o") + 1] = str(obj)
    args[args.index("-c") + 1] = str(source)
    args += ["-UNDEBUG"]
    subprocess.run(args, cwd=entry["directory"], check=True)
exe = here / "keyboard-input"
subprocess.run(["/usr/bin/c++", *objects, "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
