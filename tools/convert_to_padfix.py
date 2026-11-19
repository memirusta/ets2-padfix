"""Moves every gamepad binding in an ETS2 controls.sii onto the padfix SDK device.

Bindings on the ETS XInput device, on the DirectInput view of an XInput pad and
on the DirectInput view of a DualShock 4 / DualSense are renamed to padfix input
names and all of them end up on the `joy` alias. A `joy` alias that already
points at padfix but still carries DirectInput names (what the game leaves when
the input type is switched) is repaired the same way.

Usage: python -I convert_to_padfix.py <controls.sii> [--write] [--device sdk.padfix]
                                      [--steer-func 1.0] [--steer-dz 0.05]
Without --write it only prints the lines that would change. With --write the
original is saved as controls.sii.padfix.bak first.

When steering moves onto padfix `lx` and c_steer_func and c_steer_dz are both
still 0 (linear, no dead zone), they are set to --steer-func / --steer-dz.
Pass 0 for both to keep the linear curve. User values are never changed.
"""
import re
import shutil
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

# DirectInput view of a DualShock 4 / DualSense (same as src/playstation_profile.h).
# b13 (PS), b14 (touchpad) and b15 (DualSense mute) have no padfix input: '' drops them.
PLAYSTATION = {
    'x': 'lx', 'y': 'ly', 'z': 'rx', 'rz': 'ry', 'rx': 'lt', 'ry': 'rt',
    'b1': 'x', 'b2': 'a', 'b3': 'b', 'b4': 'y', 'b5': 'lb', 'b6': 'rb',
    'b7': 'lt_press', 'b8': 'rt_press', 'b9': 'back', 'b10': 'start', 'b11': 'ls', 'b12': 'rs',
    'pov1_up': 'dpad_up', 'pov1_down': 'dpad_down', 'pov1_left': 'dpad_left', 'pov1_right': 'dpad_right',
    'up': 'lstick_up', 'down': 'lstick_down', 'left': 'lstick_left', 'right': 'lstick_right',
    **{f'b{n}': '' for n in range(13, 33)},
}

# Sony VID 054C with DualShock 4 v1/v2, DualSense, DualSense Edge.
SONY_PRODUCT = re.compile(r'\{(05c4|09cc|0ce6|0df2)054c-0000-0000-0000-504944564944\}', re.I)

PADFIX_AXES = {'lx', 'ly', 'rx', 'ry', 'lt', 'rt'}
PADFIX_BUTTONS = {
    'a', 'b', 'x', 'y', 'lb', 'rb', 'back', 'start', 'ls', 'rs',
    'dpad_up', 'dpad_down', 'dpad_left', 'dpad_right', 'lt_press', 'rt_press',
    'lstick_up', 'lstick_down', 'lstick_left', 'lstick_right',
    'rstick_up', 'rstick_down', 'rstick_left', 'rstick_right',
}
PADFIX = PADFIX_AXES | PADFIX_BUTTONS

# On padfix x and y are buttons, so on these axis inputs they are DirectInput names.
AXIS_INPUTS = re.compile(r'j_(steer|throttle|brake|clutch|look_\w+)$')
PEDALS = {'j_throttle', 'j_brake', 'j_clutch'}

# ETS defaults bind gear1..7 to b9..b15, which on a pad are stick clicks,
# Back and Start; drop the pad part so those buttons don't also change gear.
GEAR_MIX = re.compile(r'gear[1-7]$')

DEVICE_LINE = re.compile(r'"device (joy\d?) `([^`]*)`"')
BINDING_LINE = re.compile(r'"(\w+) (\S+) `([^`]*)`"')
REFERENCE = re.compile(r'(?<![\w.])(joy\d?)\.(\w+)(\?\d+)?')


def option(name, default):
    if name not in sys.argv:
        return default
    return sys.argv[sys.argv.index(name) + 1]


def drop_term(expr, m):
    """Removes one alternative of an 'a | b | c' expression, or returns None."""
    start, end = m.start(), m.end()
    if expr[end:end + 3] == ' | ' and (start == 0 or expr[start - 3:start] == ' | '):
        return expr[:start] + expr[end + 3:]
    if end == len(expr) and expr[start - 3:start] == ' | ':
        return expr[:start - 3]
    return None


def set_constant(lines, name, value):
    pattern = re.compile(r'("constant ' + name + r' )([^"]*)(")')
    for i, line in enumerate(lines):
        m = pattern.search(line)
        if m:
            lines[i] = line[:m.start(2)] + value + line[m.end(2):]
            return m.group(2) != value
    return False


def constant_is_zero(lines, name):
    pattern = re.compile(r'"constant ' + name + r' ([^"]*)"')
    for line in lines:
        m = pattern.search(line)
        if m:
            try:
                return float(m.group(1)) == 0
            except ValueError:
                return False
    return False


def convert(text, target='sdk.padfix', steer_func=1.0, steer_dz=0.05):
    """Returns (new text, report lines, alias kinds). Exits on unsupported input."""
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(newline)

    # Which alias points at which kind of device.
    tables = {}
    padfix_aliases = set()
    for line in lines:
        m = DEVICE_LINE.search(line)
        if not m or not m.group(2):
            continue
        alias, dev = m.group(1), m.group(2)
        if dev.startswith('xinput.'):
            tables[alias] = XINPUT
        elif dev.startswith('di8.') and SONY_PRODUCT.search(dev):
            tables[alias] = PLAYSTATION
        elif dev.startswith('di8.'):
            tables[alias] = DINPUT
        elif dev == target:
            tables[alias] = None
            padfix_aliases.add(alias)
        else:
            sys.exit(f'unsupported device on {alias}: {dev}')

    bindings = []  # (line index, kind, name, expression start, expression end)
    for i, line in enumerate(lines):
        if '"device ' in line:
            continue
        m = BINDING_LINE.search(line)
        if m:
            bindings.append((i, m.group(1), m.group(2), m.start(3), m.end(3)))

    def axis_input(kind, name):
        return kind == 'input' and AXIS_INPUTS.match(name) is not None

    # A padfix alias is stale when it carries DirectInput names. rx/ry alone are
    # no evidence: padfix has them too (right stick).
    stale = set()
    for i, kind, name, start, end in bindings:
        for m in REFERENCE.finditer(lines[i][start:end]):
            alias, input_name = m.group(1), m.group(2)
            if alias in padfix_aliases and ((input_name not in PADFIX and input_name in PLAYSTATION)
                                            or (input_name in ('x', 'y') and axis_input(kind, name))):
                stale.add(alias)

    def source_table(alias, input_name, kind, name):
        """Table that renames this reference, or None to keep the name."""
        if alias not in padfix_aliases:
            return tables.get(alias)
        if alias not in stale:
            return None
        if (input_name not in PADFIX
                or (input_name in ('x', 'y') and axis_input(kind, name))
                or (input_name in ('rx', 'ry') and kind == 'input' and name in PEDALS)):
            return PLAYSTATION
        return None

    # Buttons already bound on padfix in a mix: a PlayStation button converted
    # onto one of them would do two things, so it is dropped instead.
    bound = {}
    for i, kind, name, start, end in bindings:
        if kind != 'mix':
            continue
        for m in REFERENCE.finditer(lines[i][start:end]):
            alias, input_name = m.group(1), m.group(2)
            if alias in padfix_aliases and source_table(alias, input_name, kind, name) is None \
                    and input_name in PADFIX_BUTTONS:
                bound.setdefault(input_name, name)

    report, unknown, pedals = [], [], []
    steer_to_lx = False
    for i, kind, name, start, end in bindings:
        expr = lines[i][start:end]
        gear = kind == 'mix' and GEAR_MIX.match(name) is not None
        for m in reversed(list(REFERENCE.finditer(expr))):
            alias, input_name = m.group(1), m.group(2)
            if alias not in tables:
                continue
            table = source_table(alias, input_name, kind, name)
            if table is None:
                if alias != 'joy':  # padfix alias moving to joy
                    expr = expr[:m.start()] + 'joy.' + expr[m.start(2):]
                continue
            if input_name not in table:
                unknown.append(f'{alias}.{input_name}')
                continue
            new = table[input_name]
            drop = new == '' or (gear and re.fullmatch(r'b\d+', input_name) is not None)
            reason = f'{alias}.{input_name} has no Stable Gamepad input (PS button, touchpad, mute), dropped' \
                if new == '' and not gear else ''
            if not drop and table is PLAYSTATION and kind == 'mix' and new in bound:
                drop = True
                if bound[new] != name:
                    reason = f'{alias}.{input_name} would become joy.{new}, which mix {bound[new]} already uses; dropped'
            if drop:
                shorter = drop_term(expr, m)
                if shorter is None:
                    report.append(f'{kind} {name}: {alias}.{input_name} left unchanged (not a plain alternative); '
                                  'rebind it in the game')
                    continue
                expr = shorter
                if reason:
                    report.append(f'{kind} {name}: {reason}')
                continue
            expr = expr[:m.start()] + 'joy.' + new + expr[m.end(2):]
            if kind == 'input' and name == 'j_steer' and new == 'lx':
                steer_to_lx = True
            if kind == 'input' and name in PEDALS and new in ('lt', 'rt'):
                pedals.append('c_jz' + name[2:])
        lines[i] = lines[i][:start] + expr + lines[i][end:]

    if unknown:
        sys.exit('unknown inputs, nothing written: ' + ', '.join(sorted(set(unknown))))

    for i, line in enumerate(lines):
        m = DEVICE_LINE.search(line)
        if m and m.group(1) == 'joy':
            lines[i] = line.replace(m.group(0), f'"device joy `{target}`"')
        elif m and m.group(1) in tables:
            lines[i] = line.replace(m.group(0), f'"device {m.group(1)} ``"')

    # lt/rt are 0..1; with c_jz* at 0 the game reads -1..1 and the pedal rests half pressed.
    for name in pedals:
        if set_constant(lines, name, '1.000000'):
            report.append(f'constant {name} set to 1 (pedal on a 0..1 trigger)')
    if steer_to_lx and constant_is_zero(lines, 'c_steer_func') and constant_is_zero(lines, 'c_steer_dz'):
        func, dz = f'{steer_func:.6f}', f'{steer_dz:.6f}'
        if set_constant(lines, 'c_steer_func', func) | set_constant(lines, 'c_steer_dz', dz):
            report.append(f'steering curve for a stick: c_steer_func {func}, c_steer_dz {dz}')

    kinds = {k: ('xinput' if v is XINPUT else 'playstation' if v is PLAYSTATION else 'dinput' if v is DINPUT
                 else 'padfix (stale)' if k in stale else 'padfix') for k, v in tables.items()}
    return newline.join(lines), report, kinds


def main():
    path = sys.argv[1]
    write = '--write' in sys.argv
    target = option('--device', 'sdk.padfix')
    steer_func = float(option('--steer-func', '1.0'))
    steer_dz = float(option('--steer-dz', '0.05'))
    if not 0 <= steer_func <= 2 or not 0 <= steer_dz < 1:
        sys.exit('--steer-func must be 0..2 and --steer-dz 0..1')

    text = open(path, 'rb').read().decode('utf-8')
    new_text, report, kinds = convert(text, target, steer_func, steer_dz)
    print('aliases:', kinds)

    newline = '\r\n' if '\r\n' in text else '\n'
    changed = 0
    for old, new in zip(text.split(newline), new_text.split(newline)):
        if old != new:
            changed += 1
            print('- ' + old.strip())
            print('+ ' + new.strip())
    for line in report:
        print('note: ' + line)
    print(f'changed lines: {changed}')
    if write and changed:
        shutil.copyfile(path, path + '.padfix.bak')
        open(path, 'wb').write(new_text.encode('utf-8'))
        print('written (backup: ' + path + '.padfix.bak)')


if __name__ == '__main__':
    main()
