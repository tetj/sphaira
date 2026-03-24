# Removes enum definitions from switch_usb.cpp that now conflict with newer libnx headers.
# Idempotent: no-op if the fix has already been applied.

set(file "${SOURCE_DIR}/source/os/switch/switch_usb.cpp")
file(READ "${file}" content)

string(FIND "${content}" "enum usb_request_recipient" needs_fix)
if(NOT needs_fix EQUAL -1)
    string(REGEX REPLACE "enum usb_request_recipient \\{[^}]*\\};\n\n" "" content "${content}")
    string(REGEX REPLACE "enum usb_request_type \\{[^}]*\\};\n\n" "" content "${content}")
    file(WRITE "${file}" "${content}")
    message(STATUS "libusbdvd: removed conflicting enum definitions (libnx compat fix)")
endif()
