"""Moves every gamepad binding in an ETS2 controls.sii onto the padfix SDK device.

Bindings on the ETS XInput device and on the DirectInput view of the pad are
renamed to padfix input names and all of them end up on the `joy` alias.

Usage: python -I convert_to_padfix.py <controls.sii> [--write] [--device sdk.padfix]
Without --write it only prints the lines that would change.
"""
import re
import sys

# ETS XInput device names (button order read from eurotrucks2.exe).
XINPUT = {
    'x': 'lx', 'y': 'ly', 'rx': 'rx', 'ry': 'ry', 'lt': 'lt', 'rt': 'rt',
    'b1': 'a', 'b2': 'b', 'b3': 'x', 'b4': 'y', 'b5': 'lb', 'b6': 'rb',
    'b7': 'lt_press', 'b8': 'rt_press', 'b9': 'ls', 'b10': 'rs', 'b11': 'back', 'b12': 'start',
    'pov1_up': 'dpad_up', 'pov1_down': 'dpad_down', 'pov1_left': 'dpad_left', 'pov1_right': 'dpad_right',
    'up': 'lstick_up', 'down': 'lstick_down', 'left': 'lstick_left', 'right': 'lstick_right',
    'rstick_up': 'rstick_up', 'rstick_down': 'rstick_down',
    'rstick_left': 'rstick_left', 'rstick_right': 'rstick_right',
}

# DirectInput view of an XInput pad: A B X Y LB RB Back Start LS RS.
# Its y/ry axes point the other way, so they are left out on purpose.
DINPUT = {
    'b1': 'a', 'b2': 'b', 'b3': 'x', 'b4': 'y', 'b5': 'lb', 'b6': 'rb',
    'b7': 'back', 'b8': 'start', 'b9': 'ls', 'b10': 'rs',
    'pov1_up': 'dpad_up', 'pov1_down': 'dpad_down', 'pov1_left': 'dpad_left', 'pov1_right': 'dpad_right',
    'up': 'lstick_up', 'down': 'lstick_down', 'left': 'lstick_left', 'right': 'lstick_right',
    'x': 'lx', 'rx': 'rx',
}

# ETS defaults bind gear1..7 to b9..b15, which on a pad are stick clicks,
# Back and Start; drop the pad part so those buttons don't also change gear.
GEAR_PAD_TERM = re.compile(r'(mix gear[1-7] `)joy\d?\.b\d+\?0 \| ')

DEVICE_LINE = re.compile(r'"device (joy\d?) `([^`]*)`"')
REFERENCE = re.compile(r'(?<![\w.])(joy\d?)\.(\w+)')


def main():
    path = sys.argv[1]
    write = '--write' in sys.argv
    target = sys.argv[sys.argv.index('--device') + 1] if '--device' in sys.argv else 'sdk.padfix'

    text = open(path, 'rb').read().decode('utf-8')
    lines = text.split('\r\n')

    # Which alias points at which kind of device.
    tables = {}
    for line in lines:
        m = DEVICE_LINE.search(line)
        if not m or not m.group(1).startswith('joy') or not m.group(2):
            continue
        dev = m.group(2)
        if dev.startswith('xinput.'):
            tables[m.group(1)] = XINPUT
        elif dev.startswith('di8.'):
            tables[m.group(1)] = DINPUT
        elif dev == target:
            tables[m.group(1)] = None  # already converted
        else:
            sys.exit(f'unsupported device on {m.group(1)}: {dev}')
    print('aliases:', {k: ('xinput' if v is XINPUT else 'dinput' if v is DINPUT else 'padfix') for k, v in tables.items()})

    unknown = []

    def rename(m):
        alias, name = m.group(1), m.group(2)
        table = tables.get(alias)
        if table is None:
            return m.group(0)
        if name not in table:
            unknown.append(f'{alias}.{name}')
            return m.group(0)
        return 'joy.' + table[name]

    out = []
    changed = 0
    for line in lines:
        new = GEAR_PAD_TERM.sub(r'\1', line)
        if '"device ' in new:
            m = DEVICE_LINE.search(new)
            if m and m.group(1) == 'joy':
                new = new.replace(m.group(0), f'"device joy `{target}`"')
            elif m and m.group(1) in tables:
                new = new.replace(m.group(0), f'"device {m.group(1)} ``"')
        else:
            new = REFERENCE.sub(rename, new)
        if new != line:
            changed += 1
            print('- ' + line.strip())
            print('+ ' + new.strip())
        out.append(new)

    if unknown:
        sys.exit('unknown inputs, nothing written: ' + ', '.join(sorted(set(unknown))))
    print(f'changed lines: {changed}')
    if write:
        open(path, 'wb').write('\r\n'.join(out).encode('utf-8'))
        print('written')


if __name__ == '__main__':
    main()
