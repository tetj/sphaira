"""
Removes enum usb_request_type and enum usb_request_recipient from
libusbdvd's switch_usb.cpp.  These are now provided by the libnx header
switch/services/usb.h (included transitively via <switch.h>), so keeping
the local definitions causes "redefinition of enum" errors with newer
devkitpro toolchains.

This script is run by CMake's PATCH_COMMAND immediately after libusbdvd is
cloned.  The working directory is the root of the cloned repository.
"""

import re

FILEPATH = "source/os/switch/switch_usb.cpp"

with open(FILEPATH, "r") as f:
    content = f.read()

# Remove conflicting enum definitions (no-op if already absent).
content = re.sub(
    r"\nenum usb_request_type\s*\{[^}]*\};\s*",
    "\n",
    content,
    flags=re.DOTALL,
)
content = re.sub(
    r"\nenum usb_request_recipient\s*\{[^}]*\};\s*",
    "\n",
    content,
    flags=re.DOTALL,
)

with open(FILEPATH, "w") as f:
    f.write(content)

print("patch_libusbdvd.py: patched", FILEPATH)
