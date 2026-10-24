"""Read-only broadcast diagnostics; run the game first.
Usage: python -I tools/check_rumble.py [seconds]
No motors are controlled by this tool.
"""
import ctypes as c
import struct
import sys
import time


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 5
    if not 0 < seconds <= 60:
        raise SystemExit('seconds must be between 0 and 60')
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [c.c_uint32, c.c_int, c.c_wchar_p]
    kernel.OpenFileMappingW.restype = c.c_void_p
    kernel.MapViewOfFile.argtypes = [c.c_void_p, c.c_uint32, c.c_uint32, c.c_uint32, c.c_size_t]
    kernel.MapViewOfFile.restype = c.c_void_p
    kernel.UnmapViewOfFile.argtypes = [c.c_void_p]
    kernel.CloseHandle.argtypes = [c.c_void_p]
    handle = kernel.OpenFileMappingW(4, False, 'Local\\padfix_rumble_v1')
    if not handle:
        raise SystemExit('No broadcast channel: start ETS2/ATS with the current plugin.')
    view = kernel.MapViewOfFile(handle, 4, 0, 0, 68)
    if not view:
        kernel.CloseHandle(handle)
        raise SystemExit('Cannot read broadcast channel.')
    nonzero_native = nonzero_output = errors = 0
    masks = set()
    try:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            raw = c.string_at(view, 68)
            command = struct.unpack('<IIIIIIHH', raw[:28])
            status = struct.unpack('<IIIIIHHIIII', raw[28:])
            if (command[1] & 1 or status[0] & 1
                    or command[1] != c.c_uint32.from_address(view + 4).value
                    or status[0] != c.c_uint32.from_address(view + 28).value):
                continue
            if command[0] != 0x424D5552:
                raise SystemExit('Broadcast channel is not initialized.')
            nonzero_native += int(bool(command[6] or command[7]))
            nonzero_output += int(bool(status[5] or status[6]) and not status[8] and bool(status[1]))
            errors |= status[10]
            masks.add(status[1])
            print('native=%d/%d enabled=%d command=%d targets=0x%X virtual=0x%X unknown=0x%X '
                  'output=%d/%d writes=%d blocked=%d failures=0x%X' % (
                      command[6], command[7], command[5], command[4], status[1], status[2], status[3],
                      status[5], status[6], status[7], status[8], status[10]), flush=True)
            time.sleep(.2)
        print('nonzero native samples:', nonzero_native, 'nonzero output samples:', nonzero_output,
              'target masks:', sorted(masks), 'failure mask:', hex(errors), flush=True)
    finally:
        kernel.UnmapViewOfFile(view)
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
