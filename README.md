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

Bind axes to **Stable Gamepad** to use merged input. Broadcast setup keeps the native rumble target stable while the steering mix continues to read merged input. After a broadcast session, the helper repairs supported padfix profiles with the game closed; unrelated wheel/keyboard profiles stay unchanged.

To move an existing native steering binding onto the already configured Stable Gamepad, with the game **closed**:

```
padfix_helper.exe --fix-merged-steering "<profile>\controls.sii"
```

This changes only `input j_steer` to the existing padfix alias's `.lx` axis, preserves device declarations and other bindings, and saves `controls.sii.padfix.bak`. Wheel/keyboard steering and profiles without padfix remain unchanged.

### Vibration broadcast and reconnects

padfix relays the game's native two-motor vibration commands to all connected XInput controllers that can represent physical hardware. No effects are synthesized. HowlControl and other virtual input sources still contribute to the merged inputs.

Inside ETS2/ATS only, XInput slot 1 is a stable facade backed by the external helper's merged state. It remains connected with neutral input when every real pad disconnects. The game sends native vibration to this facade; the helper forwards the commands to the currently connected output targets, independent of their slot. This does not consume an extra system-wide XInput slot or require a new driver.

Close the game, then configure an existing padfix profile once:

```
padfix_helper.exe --fix-broadcast-rumble "<profile>\controls.sii"
```

The installer does this for supported existing profiles. The command retains `input j_steer` on the stable in-game XInput slot 1 and keeps the actual `mix steering` on the merged SDK axis. It preserves the steering curve, inversion, dead zone, centering, other bindings and FFB gain. It uses an existing matching alias or an empty joystick alias, backs up changes, and is idempotent. Unsupported custom mixes and wheel/keyboard steering are preserved.

Physical targets are refreshed on connection changes and at least twice per second. Root-backed virtual controllers are excluded when Windows can identify them unambiguously. Missing identity information or a physical/virtual VID/PID collision is treated as unknown and remains eligible, so a real Xbox controller is not accidentally excluded. Virtual-only endpoints may receive motor commands in this fallback case; they do not have physical motors.

Output stops on the SDK's pause/menu event, XInput disable, loss of game focus, game exit, or a one-second producer watchdog timeout. After an interruption, a fresh native command is required; a held stale command is never replayed on resume. Motor write failures do not prevent delivery to other targets.

The bridge uses bundled MinHook 1.3.4 to intercept XInput 1.4 calls in the game process. Raw input and real motor writes stay in the external helper. System DLL files and controller firmware are unchanged. Update both plugin and helper together. No additional driver is installed. Only one active ETS2/ATS session is supported, as with the shared input helper. Other XInput interception layers and multiplayer mods have not been validated.

If the in-game controller UI regenerates the steering mix after rebinding, the helper restores supported padfix steering/rumble routing after the game exits. The legacy slot-specific `--fix-merged-rumble` and `--fix-steering` utilities are retained; use `--fix-broadcast-rumble` with the current broadcast plugin.

SCS documents aliases and mix expressions in its [advanced input configuration reference](https://modding.scssoft.com/wiki/Documentation/Engine/Advanced_input_configuration). MinHook's license and upstream details are under `third_party/minhook/` and included in release archives.

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
powershell -File make_release.ps1 -Version 1.1.0   dist\padfix-1.1.0.zip
```

Development tools (`tools/`):
- `host_test.cpp`: loads the DLL like the game does, registers the device and pulls events.
- `check_helper.py`: runs the helper on its own and reads the shared memory.
- `tools\test.bat`: builds and runs the multi-input merge and profile-repair regression checks, plus isolated rumble hook/broadcast tests, and builds `build\diagnose_input.exe`.
- `tools\test.bat --sdk`: also validates simultaneous inputs through shared memory and the actual plugin DLL; close the game and helper first.
- `check_rumble.py [seconds]`: read-only native command, output-target, pause/watchdog and motor-write diagnostics for the running game.
- `diagnose_input.exe [seconds]`: read-only check of all XInput slots and their merged axes/buttons; it leaves a running game/helper and game settings untouched.

## License

MIT, see [LICENSE](LICENSE). The SCS SDK headers under `sdk/` belong to SCS Software and are distributed under [their own license](sdk/sdk_license.txt).
