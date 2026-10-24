# padfix: a stable gamepad for ETS2 / ATS

An SCS SDK plugin for Euro Truck Simulator 2 and American Truck Simulator. It gives the game your physical and virtual Xbox-compatible (XInput) gamepads as **one device whose name never changes**: **"Stable Gamepad"**. Your bindings stay put when the pad turns off and back on, moves to another USB port, or Windows recognizes it differently.

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
- `padfix_helper.exe` polls all connected XInput pads about 400 times a second, merges their state, discovers reconnects, and exits when the game exits.
- ETS sometimes puts the device on its UI blacklist, which hides it in the controls menu. **After the game closes**, the helper checks `global_controls.sii` and, if the device is blacklisted, moves it to the whitelist. It shows up in the menu from the next launch.

### Simultaneous physical and virtual controllers

All connected XInput slots (up to four) contribute at the same time. For example, HowlControl can provide analog keyboard steering while an 8BitDo pad provides camera movement and buttons. Physical, virtual, USB and receiver connections use the same merge path; no device is excluded by vendor or device ancestry.

Merge rules:

- Buttons and D-pad: logical OR. A button stays held until every source holding it releases or disconnects.
- LT/RT: the largest value from any connected source, separately for each trigger.
- Each stick axis: the strongest absolute value. Equal-strength opposite directions produce zero. Full-scale -32768 and +32767 count as equal strength.
- Identical inputs are not added together; two sources cannot double steering or trigger values.
- Small analog values are preserved. Configure dead zones and sensitivity in the game.
- Empty slots are rescanned twice a second while connected slots are polled continuously. Disconnecting one source clears only that source's contribution.

Bind axes, including **steering**, to **Stable Gamepad** to use merged input. A steering binding directly on a native XInput device bypasses the merge. The helper no longer automatically moves steering to a native slot after the game exits.

To move an existing native steering binding onto the already configured Stable Gamepad, with the game **closed**:

```
padfix_helper.exe --fix-merged-steering "<profile>\controls.sii"
```

This changes only `input j_steer` to the existing padfix alias's `.lx` axis, preserves device declarations and other bindings, and saves `controls.sii.padfix.bak`. Wheel/keyboard steering and profiles without padfix remain unchanged.

The former native-steering vibration workaround bypasses merged steering. The SCS Input SDK interface used here has no vibration output; native game vibration is not guaranteed when steering is bound to Stable Gamepad. The explicit `--fix-steering <profile> [slot 1-4]` command remains available for users who intentionally choose native steering.

The helper and DLL use version 2 shared memory for the connected-slot mask. Update both binaries together.

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
4. In Steam, right-click the game â†’ **Properties â†’ Controller** â†’ **Disable Steam Input**.
5. Start the game and continue past the "advanced SDK features" warning (it appears on every launch while the plugin is installed).
6. Go to **Options â†’ Controls â†’ Input type: "Keyboard + Stable Gamepad"**, then bind your buttons under **Keys & Buttons**.

The installer adds these files:
```
<game>\bin\win_x64\plugins\padfix.dll
<game>\bin\win_x64\plugins\padfix\padfix_helper.exe
```

## Uninstall

Close the game and run `uninstall.bat`. Then bind your controls to your own gamepad again in the game.

## Limitations

- **XInput** pads only: Xbox pads and compatible ones such as 8BitDo in X-input mode. PlayStation pads are not supported directly.
- One logical pad merges up to four XInput devices. Per-device bindings are not exposed separately.
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
- `tools\test.bat`: builds and runs the multi-input merge and profile-repair regression checks, and builds `build\diagnose_input.exe`.
- `tools\test.bat --sdk`: also validates simultaneous inputs through shared memory and the actual plugin DLL; close the game and helper first.
- `diagnose_input.exe [seconds]`: read-only check of all XInput slots and their merged axes/buttons; it leaves a running game/helper and game settings untouched.

## License

MIT, see [LICENSE](LICENSE). The SCS SDK headers under `sdk/` belong to SCS Software and are distributed under [their own license](sdk/sdk_license.txt).
