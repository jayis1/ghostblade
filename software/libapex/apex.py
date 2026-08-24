"""Compatibility shim for the GhostBlade Python bindings.

Historically some scripts imported the extension as ``apex`` while the compiled
module is named ``pyapex``. Re-export the public API so both import styles work:

    import apex
    dev = apex.ApexBridge('/dev/apex_bridge0')

The helper ``open()`` mirrors the intent of the old README example and returns
an ``ApexBridge`` instance for convenience.
"""

from pyapex import *  # noqa: F401,F403 - compatibility re-export


def open(device: str = "/dev/apex_bridge0"):
    """Open the GhostBlade bridge device and return an ApexBridge handle."""
    return globals()["ApexBridge"](device)