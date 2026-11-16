Import("env")
# LVGL configuration must precede any transitive library includes.
env.Prepend(CPPPATH=[env.subst("$PROJECT_DIR/lib/WadaApp/include")])

import importlib.util
from pathlib import Path

root = Path(env.subst("$PROJECT_DIR"))
for name in ("patch_lvgl_anim_uaf", "patch_lvgl_arabic"):
    spec = importlib.util.spec_from_file_location(
        name, root / "lib/WadaApp/patches" / (name + ".py")
    )
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    patch.install(env)
