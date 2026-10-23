# padfix: a stable gamepad for ETS2 / ATS

An SCS SDK plugin for Euro Truck Simulator 2 and American Truck Simulator. It gives the game your Xbox-compatible (XInput) gamepad as **one device whose name never changes**: **"Stable Gamepad"**. Your bindings stay put when the pad turns off and back on, moves to another USB port, or Windows recognizes it differently.

## What problem does it solve?

ETS sees a gamepad through several paths at once, and each has its own problem:

- **DirectInput:** Windows can give the pad a different identity (GUID) every time it connects. ETS saves bindings against that identity, so they disappear after the pad reconnects.
- **Steam Input:** while it is on, Steam takes buttons like A/B/X and they never reach the game's XInput device.
- **The game's own XInput device:** on some setups some buttons (A, B, X, LB, stick clicks, D-pad) never reach the game, even though the same buttons read fine outside it.

padfix relies on none of these. It reads the pad **outside** the game and hands it to ETS through the official SDK input API.

## How it works

```
gamepad --XInput--> padfix_helper.exe --shared memory--> padfix.dll --SCS Input SDK--> ETS2/ATS
                    (outside the game)                    (in the plugins folder)
```

- `padfix.dll` loads with the game, starts `padfix_helper.exe` and registers the 30-input "Stable Gamepad" device.
- `padfix_helper.exe` polls the first connected XInput pad about 400 times a second, finds it again after a reconnect, and exits when the game exits.
- ETS sometimes puts the device on its UI blacklist, which hides it in the controls menu. **After the game closes**, the helper checks `global_controls.sii` and, if the device is blacklisted, moves it to the whitelist. It shows up in the menu from the next launch.

### Inputs

| Type | Inputs |
|---|---|
| Axes | `lx`, `ly`, `rx`, `ry` (sticks, -1..1; right and down are positive, like the game's own gamepad devices), `lt`, `rt` (triggers, 0..1) |
| Buttons | `a`, `b`, `x`, `y`, `lb`, `rb`, `back`, `start`, `ls`, `rs` (stick clicks) |
| D-pad | `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` |
| Triggers/sticks as buttons | `lt_press`, `rt_press`, `lstick_up/down/left/right`, `rstick_up/down/left/right` |

Example in `controls.sii`: ``device joy `sdk.padfix` `` and ``mix horn `keyboard.h?0 | joy.a?0` ``.

## Install

1. Download `padfix-x.y.z.zip` from the [Releases](../../releases) page and extract it to a folder.
2. Close the game.
3. Run `install.bat`. It finds the game in your Steam libraries. If it can't, pass the game folder:
   ```
   install.bat -GameDir "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
   ```
4. In Steam, right-click the game → **Properties → Controller** → **Disable Steam Input**.
5. Start the game and continue past the "advanced SDK features" warning (it appears on every launch while the plugin is installed).
6. Go to **Options → Controls → Input type: "Keyboard + Stable Gamepad"**, then bind your buttons under **Keys & Buttons**.

The installer adds these files:
```
<game>\bin\win_x64\plugins\padfix.dll
<game>\bin\win_x64\plugins\padfix\padfix_helper.exe
```

## Uninstall

Close the game and run `uninstall.bat`. Then bind your controls to your own gamepad again in the game.

## Limitations

- **XInput** pads only: Xbox pads and compatible ones such as 8BitDo in X-input mode. PlayStation pads are not supported directly.
- One pad: the first connected XInput pad is used.
- Windows (64-bit) only.
- Not tested with online mods (e.g. TruckersMP).
- The binaries are not code-signed, so Windows SmartScreen or antivirus software may warn about them. You can build them yourself from this repository.

## Moving existing bindings

If your bindings are on the game's XInput or DirectInput device, `tools/convert_to_padfix.py` moves all of them to Stable Gamepad with the same buttons (with the game closed):

```
python tools/convert_to_padfix.py "<Documents>\Euro Truck Simulator 2\steam_profiles\<profile>\controls.sii"          (preview only)
python tools/convert_to_padfix.py "<Documents>\Euro Truck Simulator 2\steam_profiles\<profile>\controls.sii" --write  (writes the file)
```

Back up the file first.

## Building

Requires Visual Studio 2022 or later with "Desktop development with C++".

```
build.bat                                          build\padfix.dll and build\padfix_helper.exe
powershell -File make_release.ps1 -Version 1.0.2   dist\padfix-1.0.2.zip
```

Development tools (`tools/`):
- `host_test.cpp`: loads the DLL like the game does, registers the device and pulls events.
- `check_helper.py`: runs the helper on its own and reads the shared memory.

## License

MIT, see [LICENSE](LICENSE). The SCS SDK headers under `sdk/` belong to SCS Software and are distributed under [their own license](sdk/sdk_license.txt).
