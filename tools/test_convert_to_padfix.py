"""Regression checks for convert_to_padfix.py: python -I tools/test_convert_to_padfix.py

Uses the same field profile as tools/playstation_profile_fixture.h.
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import convert_to_padfix as convert  # noqa: E402

SCRIPT = os.path.join(HERE, 'convert_to_padfix.py')
DS4 = "di8.'{6F1D2B60-D5A0-11CF-BFC7-444553540000}|{09CC054C-0000-0000-0000-504944564944}'"
STEERING_MIX = 'mix steering `pow(normalize(abs(j_steer?0), c_steer_dz), 1 + c_steer_func * 0.5) * sign(j_steer?0)`'


def profile(config, nl='\r\n'):
    lines = ['SiiNunit', '{', 'controls : .controls {', f' config_lines: {len(config)}']
    lines += [f' config_lines[{i}]: "{line}"' for i, line in enumerate(config)]
    return nl.join(lines + ['}', '', '}', ''])


def field_config(joy='sdk.padfix'):
    return [
        'device keyboard `di8.keyboard`', 'device mouse `fusion.mouse`', f'device joy `{joy}`',
        'device joy2 ``', 'device joy3 ``', 'device joy4 ``', 'device joy5 ``', 'device joy6 ``',
        'constant c_steer_func 0.000000', 'constant c_steer_dz 0.000000',
        'constant c_jzthrottle 1.000000', 'constant c_jzbrake 0.000000',
        'input j_steer `joy.x`', 'input j_throttle `joy.ry`', 'input j_brake `joy.rx`',
        'input j_look_ud `joy.rz`', 'input j_look_lr `joy.z`',
        STEERING_MIX,
        'mix airhorn `unbound?0 | joy.b6?0 | semantical.airhorn?0`',
        'mix cruiectrlinc `unbound?0 | joy.b1?0 | semantical.cruiectrlinc?0`',
        'mix cam1 `keyboard.key1?0 | joy.pov1_left?0 | semantical.cam1?0`',
        'mix cam2 `keyboard.key2?0 | joy.pov1_down?0 | semantical.cam2?0`',
        'mix parkingbrake `keyboard.space?0 | joy.ls?0 | semantical.parkingbrake?0`',
        'mix drive `joy.b11?0 | semantical.drive?0`', 'mix reverse `joy.b12?0 | semantical.reverse?0`',
        'mix gear1 `joy.b15?0 | semantical.gear1?0`', 'mix gear2 `joy.b9?0 | semantical.gear2?0`',
        'mix gear3 `joy.b10?0 | semantical.gear3?0`', 'mix gear4 `joy.b11?0 | semantical.gear4?0`',
        'mix gear5 `joy.b12?0 | semantical.gear5?0`', 'mix gear6 `joy.b13?0 | semantical.gear6?0`',
        'mix gear7 `joy.b14?0 | semantical.gear7?0`',
    ]


def field_expected():
    return [
        'device keyboard `di8.keyboard`', 'device mouse `fusion.mouse`', 'device joy `sdk.padfix`',
        'device joy2 ``', 'device joy3 ``', 'device joy4 ``', 'device joy5 ``', 'device joy6 ``',
        'constant c_steer_func 1.000000', 'constant c_steer_dz 0.050000',
        'constant c_jzthrottle 1.000000', 'constant c_jzbrake 1.000000',
        'input j_steer `joy.lx`', 'input j_throttle `joy.rt`', 'input j_brake `joy.lt`',
        'input j_look_ud `joy.ry`', 'input j_look_lr `joy.rx`',
        STEERING_MIX,
        'mix airhorn `unbound?0 | joy.rb?0 | semantical.airhorn?0`',
        'mix cruiectrlinc `unbound?0 | joy.x?0 | semantical.cruiectrlinc?0`',
        'mix cam1 `keyboard.key1?0 | joy.dpad_left?0 | semantical.cam1?0`',
        'mix cam2 `keyboard.key2?0 | joy.dpad_down?0 | semantical.cam2?0`',
        'mix parkingbrake `keyboard.space?0 | joy.ls?0 | semantical.parkingbrake?0`',
        'mix drive `semantical.drive?0`', 'mix reverse `joy.rs?0 | semantical.reverse?0`',
    ] + [f'mix gear{n} `semantical.gear{n}?0`' for n in range(1, 8)]


class ConvertTest(unittest.TestCase):
    def test_field_profile(self):
        for nl in ('\n', '\r\n'):
            text, report, kinds = convert.convert(profile(field_config(), nl))
            self.assertEqual(text, profile(field_expected(), nl))
            self.assertEqual(kinds, {'joy': 'padfix (stale)'})
            self.assertIn('mix drive: joy.b11 would become joy.ls, which mix parkingbrake already uses; dropped', report)
            self.assertIn('constant c_jzbrake set to 1 (pedal on a 0..1 trigger)', report)
            self.assertIn('steering curve for a stick: c_steer_func 1.000000, c_steer_dz 0.050000', report)
            again, report, _ = convert.convert(text)
            self.assertEqual((again, report), (text, []))

    def test_sony_alias(self):
        config = field_config(DS4)
        config[22] = 'mix parkingbrake `keyboard.space?0 | semantical.parkingbrake?0`'
        expected = field_expected()
        expected[22] = config[22]
        expected[23] = 'mix drive `joy.ls?0 | semantical.drive?0`'
        text, _, kinds = convert.convert(profile(config))
        self.assertEqual(kinds, {'joy': 'playstation'})
        self.assertEqual(text, profile(expected))

    def test_merge_and_user_curve(self):
        text, report, _ = convert.convert(profile([
            'device joy `sdk.padfix`', f'device joy2 `{DS4}`',
            'constant c_steer_func 0.500000', 'constant c_steer_dz 0.000000', 'constant c_jzclutch 0.000000',
            'input j_steer `joy2.x`', 'input j_clutch `joy2.rx`', 'input j_look_lr `joy.rx`',
            'mix horn `keyboard.h?0 | joy2.b2?0`', 'mix lights `joy.a?0 | keyboard.l?0`',
            'mix menu `joy2.b13?0 | keyboard.escape?0`',
        ]))
        for line in ('device joy2 ``', 'input j_steer `joy.lx`', 'input j_clutch `joy.lt`', 'input j_look_lr `joy.rx`',
                     'constant c_jzclutch 1.000000', 'constant c_steer_func 0.500000', 'constant c_steer_dz 0.000000',
                     'mix horn `keyboard.h?0`', 'mix menu `keyboard.escape?0`'):
            self.assertIn(f'"{line}"', text)
        self.assertIn('mix horn: joy2.b2 would become joy.a, which mix lights already uses; dropped', report)

    def test_correct_profile_untouched(self):
        before = profile([
            'device joy `sdk.padfix`', 'constant c_steer_func 0.000000', 'constant c_steer_dz 0.000000',
            'input j_steer `joy.lx`', 'input j_throttle `joy.ry`', 'input j_look_lr `joy.rx`', 'input j_look_ud `joy.ry`',
            'mix horn `keyboard.h?0 | joy.x?0`',
        ])
        self.assertEqual(convert.convert(before)[:2], (before, []))

    def test_xinput_steering_gets_curve(self):
        text, _, _ = convert.convert(profile([
            'device joy `xinput.xinput_gamepad_1`', 'constant c_steer_func 0.000000', 'constant c_steer_dz 0.000000',
            'constant c_jzbrake 0.000000', 'input j_steer `joy.x`', 'input j_brake `joy.lt`',
            'mix gear1 `joy.b9?0 | semantical.gear1?0`',
        ]), steer_func=0.5, steer_dz=0.1)
        for line in ('device joy `sdk.padfix`', 'input j_steer `joy.lx`', 'constant c_steer_func 0.500000',
                     'constant c_steer_dz 0.100000', 'constant c_jzbrake 1.000000', 'mix gear1 `semantical.gear1?0`'):
            self.assertIn(f'"{line}"', text)

    def test_unknown_input_aborts(self):
        config = field_config() + ['mix odd `joy.slider1?0`']
        with self.assertRaises(SystemExit) as error:
            convert.convert(profile(config))
        self.assertIn('joy.slider1', str(error.exception))

    def test_preview_write_and_backup(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, 'controls.sii')
            before = profile(field_config()).encode('utf-8')
            with open(path, 'wb') as f:
                f.write(before)

            def run(*args):
                return subprocess.run([sys.executable, '-I', SCRIPT, path, *args],
                                      capture_output=True, text=True, check=True).stdout

            self.assertIn('changed lines: 21', run())
            self.assertEqual(open(path, 'rb').read(), before, 'preview writes nothing')
            self.assertFalse(os.path.exists(path + '.padfix.bak'))
            self.assertIn('written', run('--write'))
            self.assertEqual(open(path + '.padfix.bak', 'rb').read(), before)
            self.assertEqual(open(path, 'rb').read().decode('utf-8'), profile(field_expected()))
            self.assertIn('changed lines: 0', run('--write'))
            self.assertEqual(open(path + '.padfix.bak', 'rb').read(), before, 'no-op run keeps the backup')


if __name__ == '__main__':
    unittest.main()
