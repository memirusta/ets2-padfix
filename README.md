# padfix: a stable gamepad for ETS2 / ATS

An SCS SDK plugin for Euro Truck Simulator 2 and American Truck Simulator. It gives the game your physical and virtual Xbox-compatible (XInput) gamepads and PlayStation controllers (DualShock 4, DualSense) as **one device whose name never changes**: **"Stable Gamepad"**. Your bindings stay put when the pad turns off and back on, moves to another USB port, or Windows recognizes it differently.

## What problem does it solve?

ETS sees a gamepad through several paths at once, and each has its own problem:

- **DirectInput:** Windows can give the pad a different identity (GUID) every time it connects. ETS saves bindings against that identity, so they disappear after the pad reconnects.
- **Steam Input:** while it is on, Steam takes buttons like A/B/X and they never reach the game's XInput device.
- **The game's own XInput device:** on some setups some buttons (A, B, X, LB, stick clicks, D-pad) never reach the game, even though the same buttons read fine outside it.

padfix relies on none of these. It reads the pad **outside** the game and hands it to ETS through the official SDK input API.

## How it works

```
gamepad --XInput or HID--> padfix_helper.exe --shared memory--> padfix.dll --SCS Input SDK--> ETS2/ATS
                           (outside the game)                    (in the plugins folder)
```

- `padfix.dll` loads with the game, starts `padfix_helper.exe` and registers the 30-input "Stable Gamepad" device.
- `padfix_helper.exe` polls all connected XInput pads and PlayStation controllers about 400 times a second, merges their state, discovers reconnects, and exits when the game exits.
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

### PlayStation controllers

DualShock 4 (both versions) and DualSense (including Edge) are read directly over HID, by USB cable or Bluetooth, and merged with the XInput pads by the same rules. Steam Input and DS4Windows are not needed.

| PlayStation | Stable Gamepad |
|---|---|
| Cross, Circle, Square, Triangle | `a`, `b`, `x`, `y` |
| L1, R1 | `lb`, `rb` |
| L2, R2 (analog) | `lt`, `rt` |
| Share / Create, Options | `back`, `start` |
| L3, R3 | `ls`, `rs` |
| Sticks, D-pad | `lx` `ly` `rx` `ry`, D-pad |

The PS button and the touchpad are not mapped. The game's vibration reaches PlayStation controllers too, under the same pause, focus and stop rules; padfix only sets the motors and leaves the lightbar alone.

- Controllers are opened shared, so Steam and other programs keep working with them. One that another program opened exclusively or hid (for example DS4Windows with HidHide) is skipped; it can still arrive through that program's virtual Xbox controller. A controller read both ways is not doubled, but the other program may override padfix's vibration.
- Over Bluetooth, padfix switches the controller to its full report mode, as Steam does. Programs that only use the basic DirectInput report may stop seeing it until it reconnects.
- New controllers are picked up within two seconds. A controller that sends nothing for a second (for example a dropped Bluetooth link) stops contributing until it reports again.
- The Sony USB wireless adapter is not supported.

#### Profiles set up on the PlayStation controller's DirectInput device

A profile that was bound on the controller's own DirectInput device (`di8.'{...}|{09CC054C-...}'` and the like) uses names that mean something else on Stable Gamepad: `x` is the left stick there but the Square button on padfix, `rx`/`ry` are L2/R2, `b1`-`b14` are buttons. When the game is switched to Stable Gamepad, it keeps those names, so steering, pedals and most buttons stop working. The installer converts such profiles, and the helper does it again after each game session (with the game closed). To run it yourself, close the game:

```
padfix_helper.exe --fix-playstation-bindings "<profile>\controls.sii" | more
```

- A `joy` alias on a DualShock 4 (`054C:05C4`, `054C:09CC`) or DualSense (`054C:0CE6`, Edge `054C:0DF2`) is pointed at Stable Gamepad, or merged into the existing Stable Gamepad alias. A Stable Gamepad alias that still carries DirectInput names (`z`, `rz`, `b1`-`b14`, `pov1_*`, or `x`/`y` on steering, pedals or look) is repaired. Keyboard, mouse, wheel and other aliases are never touched.
- Sticks: `x y z rz` become `lx ly rx ry`; L2/R2 (`rx`/`ry`) become `lt`/`rt`, and the pedal's `c_jzthrottle`/`c_jzbrake`/`c_jzclutch` is set to 1 so it does not rest half pressed. Buttons: Square `x`, Cross `a`, Circle `b`, Triangle `y`, L1/R1 `lb`/`rb`, L2/R2 click `lt_press`/`rt_press`, Share `back`, Options `start`, L3/R3 `ls`/`rs`, D-pad `dpad_*`.
- The PS button, the touchpad and the DualSense mute button have no Stable Gamepad input; those bindings are dropped, as are the game's default gear1-7 pad buttons. A button that is already bound to something else on Stable Gamepad is dropped too (for example drive on L3 while L3 is the parking brake), so one button never does two things. Every dropped binding is printed; rebind it in the game if you need it.
- On a Stable Gamepad alias, `rx`/`ry` count as DirectInput names only on throttle, brake and clutch, and only when the profile has other DirectInput names, because they are padfix's right stick too.
- It saves `controls.sii.padfix.bak` and, because later fixes replace that file, also `controls.sii.padfix-playstation.bak`. It is idempotent, and writes nothing if the profile contains an input it does not know. Exit code 0 = unchanged, 2 = converted, 1 = error.

**Steering curve.** DirectInput profiles often steer with `c_steer_func 0` and `c_steer_dz 0`: the stick maps linearly to the steering wheel, which feels far too fast on a pad. When the conversion moves steering onto `lx` and both are still 0, it sets the game's own steering curve to `c_steer_func 1` and `c_steer_dz 0.05` (Options > Controls: steering non-linearity and dead zone). The game steers with `pow(normalize(abs(j), c_steer_dz), 1 + c_steer_func * 0.5)`, so this is an exponent of 1.5 with a 5% dead zone: half stick gives about a third of full lock. padfix itself still sends the raw stick value. Values you set yourself are never changed, and the automatic repair after a game session never touches the curve. Choose other values with `--steer-func <0-2>` and `--steer-dz <0-1>`, or pass `0` for both to keep the linear curve.

Bind axes to **Stable Gamepad** to use merged input. Broadcast setup keeps the native rumble target stable while the steering mix continues to read merged input. After a broadcast session, the helper repairs supported padfix profiles with the game closed; unrelated wheel/keyboard profiles stay unchanged.

To move an existing native steering binding onto the already configured Stable Gamepad, with the game **closed**:

```
padfix_helper.exe --fix-merged-steering "<profile>\controls.sii"
```

This changes only `input j_steer` to the existing padfix alias's `.lx` axis, preserves device declarations and other bindings, and saves `controls.sii.padfix.bak`. Wheel/keyboard steering and profiles without padfix remain unchanged.

### Vibration broadcast and reconnects

padfix relays the game's native two-motor vibration commands to all connected XInput controllers that can represent physical hardware, and to PlayStation controllers. No effects are synthesized. HowlControl and other virtual input sources still contribute to the merged inputs.

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

1. Download `padfix-x.y.z.zip` from the [Releases](../../releases) page and extract the whole zip to a folder.
2. Close the game.
3. Run `padfix_setup.exe` (`install.bat` does the same). It finds the game in your Steam libraries. If it can't, pass the game folder:
   ```
   padfix_setup.exe --game-dir "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
   ```
   Add `--files-only` to copy the plugin without changing the game's settings or your profiles.
4. In Steam, right-click the game > **Properties > Controller** > **Disable Steam Input**.
5. Start the game and continue past the "advanced SDK features" warning (it appears on every launch while the plugin is installed).
6. Go to **Options > Controls > Input type: "Keyboard + Stable Gamepad"**, then bind your buttons under **Keys & Buttons**.

The installer adds these files:
```
<game>\bin\win_x64\plugins\padfix.dll
<game>\bin\win_x64\plugins\padfix\padfix_helper.exe
```

## Uninstall

Close the game and run `uninstall.bat` (or `padfix_setup.exe --uninstall`). Then bind your controls to your own gamepad again in the game.

## Limitations

- **XInput** pads (Xbox pads and compatible ones such as 8BitDo in X-input mode) and Sony's DualShock 4 and DualSense. Other DirectInput-only pads, including third-party PlayStation-style pads with their own IDs, are not supported.
- One logical pad merges up to four XInput devices plus PlayStation controllers. Per-device bindings are not exposed separately.
- Windows (64-bit) only.
- Not tested with online mods (e.g. TruckersMP).
- Unsigned builds are blocked outright by Windows 11 **Smart App Control**, which has no "run anyway" option, and SmartScreen or antivirus software may warn about them. Only code-signed releases install while Smart App Control is on.

## Moving existing bindings

If your bindings are on the game's XInput or DirectInput device (including a DualShock 4 or DualSense), `tools/convert_to_padfix.py` moves all of them to Stable Gamepad with the same buttons (with the game closed). It applies the same PlayStation rules and steering curve as `--fix-playstation-bindings`, and also repairs a Stable Gamepad alias that still carries DirectInput names:

```
python tools/convert_to_padfix.py "<Documents>\Euro Truck Simulator 2\steam_profiles\<profile>\controls.sii"          (preview only)
python tools/convert_to_padfix.py "<Documents>\Euro Truck Simulator 2\steam_profiles\<profile>\controls.sii" --write  (writes the file)
```

`--write` saves the original as `controls.sii.padfix.bak` first. `--steer-func` and `--steer-dz` choose the steering curve (see above).

## Building

Requires Visual Studio 2022 or later with "Desktop development with C++".

```
build.bat                                          build\padfix.dll, build\padfix_helper.exe, build\padfix_setup.exe
powershell -File make_release.ps1 -Version 1.1.0   dist\padfix-1.1.0.zip
```

To sign a release, pass the SHA-1 thumbprint of a code-signing certificate in your Windows certificate store; `make_release.ps1` then signs and verifies all three binaries with `signtool` from the Windows SDK before packing them:

```
powershell -File make_release.ps1 -Version 1.1.0 -SignThumbprint <thumbprint>
```

Development tools (`tools/`):
- `host_test.cpp`: loads the DLL like the game does, registers the device and pulls events.
- `check_helper.py`: runs the helper on its own and reads the shared memory.
- `tools\test.bat`: builds and runs the multi-input merge and profile-repair regression checks, the PlayStation profile conversion checks (`profile_test.cpp`, and `test_convert_to_padfix.py` when Python is installed), the PlayStation report/rumble checks, plus isolated rumble hook/broadcast tests, and builds `build\diagnose_input.exe`.
- `tools\test.bat --sdk`: also validates simultaneous inputs through shared memory and the actual plugin DLL; close the game and helper first.
- `check_rumble.py [seconds]`: read-only native command, output-target, pause/watchdog and motor-write diagnostics for the running game.
- `diagnose_input.exe [seconds] [--rumble]`: check of all XInput slots and PlayStation controllers and their merged axes/buttons; it leaves a running game/helper and game settings untouched. It writes nothing to controllers unless `--rumble` is given, which makes PlayStation controllers vibrate gently while Cross is held.

## License

MIT, see [LICENSE](LICENSE). The SCS SDK headers under `sdk/` belong to SCS Software and are distributed under [their own license](sdk/sdk_license.txt).
