"""Normalize PEP 440 post releases for PlatformIO's semver compatibility check."""
try:
    import platformio
    if platformio.__version__.startswith("6.2.0.post"):
        platformio.__version__ = "6.2.0"
except ImportError:
    pass
