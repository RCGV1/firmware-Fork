Import("env")
from pathlib import Path

root = Path(env.subst("$PROJECT_DIR")) / "lib" / "WadaApp"
# The SDK also has a Mesh.h; these private UI headers must win within this library.
env.Prepend(
    CPPPATH=[
        str(root / part)
        for part in (
            "port",
            "backend",
            "vendor",
            "vendor/ui-touch",
            "core",
            "ed25519",
            "lib/lua/src",
            "include",
        )
    ]
)
