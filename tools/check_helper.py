"""Starts padfix_helper.exe with this process as its parent and checks that it
publishes the pad state. The helper exits on its own when this script ends.

Usage: python -I check_helper.py <path to padfix_helper.exe> [seconds to watch]
"""
import mmap
import os
import struct
import subprocess
import sys
import time

FORMAT = '<IIIIIHBBhhhh'  # matches padfix_state_t in src/shared.h
SIZE = struct.calcsize(FORMAT)
NAMES = ['magic', 'seq', 'heartbeat', 'connected', 'slot', 'buttons',
         'left_trigger', 'right_trigger', 'thumb_lx', 'thumb_ly', 'thumb_rx', 'thumb_ry']
BUTTONS = [(0x0001, 'DPAD_UP'), (0x0002, 'DPAD_DOWN'), (0x0004, 'DPAD_LEFT'), (0x0008, 'DPAD_RIGHT'),
           (0x0010, 'START'), (0x0020, 'BACK'), (0x0040, 'LS'), (0x0080, 'RS'),
           (0x0100, 'LB'), (0x0200, 'RB'), (0x1000, 'A'), (0x2000, 'B'), (0x4000, 'X'), (0x8000, 'Y')]


def read(shm):
    shm.seek(0)
    return dict(zip(NAMES, struct.unpack(FORMAT, shm.read(SIZE))))


def main():
    helper = sys.argv[1]
    watch = float(sys.argv[2]) if len(sys.argv) > 2 else 0
    proc = subprocess.Popen([helper, str(os.getpid())])
    time.sleep(1.0)
    shm = mmap.mmap(-1, SIZE, tagname='Local\\padfix_state_v1')
    first = read(shm)
    time.sleep(0.3)
    second = read(shm)
    print('helper running:', proc.poll() is None)
    print('magic ok:', first['magic'] == 0x46444150)
    print('heartbeat advancing:', second['heartbeat'] - first['heartbeat'], 'polls in 0.3s')
    print('pad connected:', bool(second['connected']), '(XInput slot %d)' % (second['slot'] + 1))
    if watch:
        print('watching buttons for %.0fs...' % watch, flush=True)
        prev = 0
        end = time.time() + watch
        while time.time() < end:
            s = read(shm)
            down = [n for bit, n in BUTTONS if s['buttons'] & bit and not prev & bit]
            if down:
                print('  pressed', ' + '.join(down), flush=True)
            prev = s['buttons']
            time.sleep(0.01)


if __name__ == '__main__':
    main()
