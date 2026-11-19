#pragma once

// The controls.sii seen in the field: the profile was set up on a DualShock 4's
// DirectInput device, then the game switched the joy alias to Stable Gamepad
// and kept the DirectInput names. Shared by profile_test.cpp (portable) and
// controller_merge_test.cpp (through the helper's file fix).

#include <string>
#include <vector>

inline std::string sii_profile(const std::vector<std::string> &config, const std::string &nl)
{
	std::string text = "SiiNunit" + nl + "{" + nl + "controls : .controls {" + nl
		+ " config_lines: " + std::to_string(config.size()) + nl;
	for (size_t i = 0; i < config.size(); ++i) {
		text += " config_lines[" + std::to_string(i) + "]: \"" + config[i] + "\"" + nl;
	}
	return text + "}" + nl + nl + "}" + nl;
}

const char PS_STEERING_MIX[] = "mix steering `pow(normalize(abs(j_steer?0), c_steer_dz), 1 + c_steer_func * 0.5) * sign(j_steer?0)`";

inline std::vector<std::string> ds4_field_config(const std::string &joy_device = "sdk.padfix")
{
	return {
		"device keyboard `di8.keyboard`",
		"device mouse `fusion.mouse`",
		"device joy `" + joy_device + "`",
		"device joy2 ``", "device joy3 ``", "device joy4 ``", "device joy5 ``", "device joy6 ``",
		"constant c_steer_func 0.000000",
		"constant c_steer_dz 0.000000",
		"constant c_jzthrottle 1.000000",
		"constant c_jzbrake 0.000000",
		"input j_steer `joy.x`",
		"input j_throttle `joy.ry`",
		"input j_brake `joy.rx`",
		"input j_look_ud `joy.rz`",
		"input j_look_lr `joy.z`",
		PS_STEERING_MIX,
		"mix airhorn `unbound?0 | joy.b6?0 | semantical.airhorn?0`",
		"mix cruiectrlinc `unbound?0 | joy.b1?0 | semantical.cruiectrlinc?0`",
		"mix cam1 `keyboard.key1?0 | joy.pov1_left?0 | semantical.cam1?0`",
		"mix cam2 `keyboard.key2?0 | joy.pov1_down?0 | semantical.cam2?0`",
		"mix parkingbrake `keyboard.space?0 | joy.ls?0 | semantical.parkingbrake?0`",
		"mix drive `joy.b11?0 | semantical.drive?0`",
		"mix reverse `joy.b12?0 | semantical.reverse?0`",
		"mix gear1 `joy.b15?0 | semantical.gear1?0`",
		"mix gear2 `joy.b9?0 | semantical.gear2?0`",
		"mix gear3 `joy.b10?0 | semantical.gear3?0`",
		"mix gear4 `joy.b11?0 | semantical.gear4?0`",
		"mix gear5 `joy.b12?0 | semantical.gear5?0`",
		"mix gear6 `joy.b13?0 | semantical.gear6?0`",
		"mix gear7 `joy.b14?0 | semantical.gear7?0`",
	};
}

// The same profile after conversion (steering curve 1.0 / 0.05).
inline std::vector<std::string> ds4_field_expected()
{
	return {
		"device keyboard `di8.keyboard`",
		"device mouse `fusion.mouse`",
		"device joy `sdk.padfix`",
		"device joy2 ``", "device joy3 ``", "device joy4 ``", "device joy5 ``", "device joy6 ``",
		"constant c_steer_func 1.000000",
		"constant c_steer_dz 0.050000",
		"constant c_jzthrottle 1.000000",
		"constant c_jzbrake 1.000000",
		"input j_steer `joy.lx`",
		"input j_throttle `joy.rt`",
		"input j_brake `joy.lt`",
		"input j_look_ud `joy.ry`",
		"input j_look_lr `joy.rx`",
		PS_STEERING_MIX,
		"mix airhorn `unbound?0 | joy.rb?0 | semantical.airhorn?0`",
		"mix cruiectrlinc `unbound?0 | joy.x?0 | semantical.cruiectrlinc?0`",
		"mix cam1 `keyboard.key1?0 | joy.dpad_left?0 | semantical.cam1?0`",
		"mix cam2 `keyboard.key2?0 | joy.dpad_down?0 | semantical.cam2?0`",
		"mix parkingbrake `keyboard.space?0 | joy.ls?0 | semantical.parkingbrake?0`",
		"mix drive `semantical.drive?0`",
		"mix reverse `joy.rs?0 | semantical.reverse?0`",
		"mix gear1 `semantical.gear1?0`",
		"mix gear2 `semantical.gear2?0`",
		"mix gear3 `semantical.gear3?0`",
		"mix gear4 `semantical.gear4?0`",
		"mix gear5 `semantical.gear5?0`",
		"mix gear6 `semantical.gear6?0`",
		"mix gear7 `semantical.gear7?0`",
	};
}

// DirectInput device of a USB DualShock 4 v2, as the game writes it.
const char DS4_DINPUT_DEVICE[] = "di8.'{6F1D2B60-D5A0-11CF-BFC7-444553540000}|{09CC054C-0000-0000-0000-504944564944}'";
