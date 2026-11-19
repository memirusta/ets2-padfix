// Checks the PlayStation DirectInput -> Stable Gamepad profile conversion.
// Portable: builds with MSVC (tools\test.bat) and with any C++11 compiler.
#include <stdio.h>
#include <stdlib.h>

#include "../src/playstation_profile.h"
#include "playstation_profile_fixture.h"

unsigned checks = 0;
void check(bool ok, const std::string &message)
{
	++checks;
	if (! ok) { fprintf(stderr, "FAIL: %s\n", message.c_str()); exit(1); }
}

bool has_line(const std::string &text, const std::string &line)
{
	return text.find("\"" + line + "\"") != std::string::npos;
}

bool reported(const ps_profile_result_t &result, const std::string &part)
{
	for (const std::string &line : result.report) {
		if (line.find(part) != std::string::npos) return true;
	}
	return false;
}

void unchanged_case(const std::vector<std::string> &config, const char *message)
{
	const std::string before = sii_profile(config, "\r\n");
	const ps_profile_result_t result = convert_playstation_profile(before);
	check(! result.changed && ! result.error && result.text == before, message);
}

int main()
{
	const char *newlines[] = { "\n", "\r\n" };
	for (const char *nl : newlines) {
		// The field profile: stale DirectInput names on an sdk.padfix alias.
		const std::string before = sii_profile(ds4_field_config(), nl);
		const ps_profile_result_t result = convert_playstation_profile(before);
		check(result.changed && ! result.error, "field profile converted");
		check(result.text == sii_profile(ds4_field_expected(), nl), "field profile matches the expected bindings, newlines kept");
		check(reported(result, "mix drive: joy.b11 would become joy.ls, which mix parkingbrake already uses; dropped"), "drive/parking brake collision reported");
		check(reported(result, "c_jzbrake set to 1"), "brake on a trigger reported");
		check(reported(result, "c_steer_func 1.000000, c_steer_dz 0.050000"), "steering curve reported");
		check(! reported(result, "gear"), "default gear terms dropped silently");
		const ps_profile_result_t again = convert_playstation_profile(result.text);
		check(! again.changed && ! again.error && again.report.empty(), "second run is a no-op");

		// The same bindings while joy still points at the DualShock 4 (before
		// joy.ls was bound on padfix).
		std::vector<std::string> sony_config = ds4_field_config(DS4_DINPUT_DEVICE);
		sony_config[22] = "mix parkingbrake `keyboard.space?0 | semantical.parkingbrake?0`";
		const ps_profile_result_t sony = convert_playstation_profile(sii_profile(sony_config, nl));
		std::vector<std::string> expected = ds4_field_expected();
		expected[22] = sony_config[22];
		expected[23] = "mix drive `joy.ls?0 | semantical.drive?0`";
		check(sony.changed && ! sony.error, "di8 DualShock 4 alias converted");
		check(sony.text == sii_profile(expected, nl), "Sony alias points at Stable Gamepad with the same bindings");
		check(convert_playstation_profile(sony.text).changed == false, "di8 conversion idempotent");
	}

	// A Sony alias merged into an existing padfix alias; keyboard untouched.
	std::vector<std::string> merge = {
		"device keyboard `di8.keyboard`",
		"device joy `sdk.padfix`",
		std::string("device joy2 `") + DS4_DINPUT_DEVICE + "`",
		"constant c_steer_func 0.500000",
		"constant c_steer_dz 0.000000",
		"constant c_jzclutch 0.000000",
		"input j_steer `joy2.x`",
		"input j_clutch `joy2.rx`",
		"input j_look_lr `joy.rx`",
		"mix horn `keyboard.h?0 | joy2.b2?0`",
		"mix lights `joy.a?0 | keyboard.l?0`",
		"mix menu `joy2.b13?0 | keyboard.escape?0`",
		"mix combo `joy2.b14?0 & joy2.b5?0`",
	};
	const ps_profile_result_t merged = convert_playstation_profile(sii_profile(merge, "\r\n"));
	check(merged.changed && ! merged.error, "merge into existing padfix alias");
	check(has_line(merged.text, "device joy2 ``") && has_line(merged.text, "device joy `sdk.padfix`"), "Sony alias emptied, padfix alias kept");
	check(has_line(merged.text, "input j_steer `joy.lx`") && has_line(merged.text, "input j_look_lr `joy.rx`"), "steering moved, padfix right stick kept");
	check(has_line(merged.text, "input j_clutch `joy.lt`") && has_line(merged.text, "constant c_jzclutch 1.000000"), "clutch on L2 with c_jzclutch 1");
	check(has_line(merged.text, "constant c_steer_func 0.500000") && has_line(merged.text, "constant c_steer_dz 0.000000"), "user's steering curve preserved");
	check(has_line(merged.text, "mix horn `keyboard.h?0`"), "Cross collides with A on lights: dropped");
	check(reported(merged, "mix horn: joy2.b2 would become joy.a, which mix lights already uses"), "collision reported");
	check(has_line(merged.text, "mix menu `keyboard.escape?0`") && reported(merged, "joy2.b13 has no Stable Gamepad input"), "PS button dropped and reported");
	check(has_line(merged.text, "mix combo `joy2.b14?0 & joy.lb?0`") && reported(merged, "joy2.b14 left unchanged"), "touchpad inside a combination left and reported");

	// Steering curve on request only, and never over user values.
	ps_profile_options_t keep;
	keep.steer_func = keep.steer_dz = PS_STEER_KEEP;
	const ps_profile_result_t kept = convert_playstation_profile(sii_profile(ds4_field_config(), "\n"), keep);
	check(has_line(kept.text, "constant c_steer_func 0.000000") && has_line(kept.text, "constant c_steer_dz 0.000000"), "repair mode keeps the steering constants");
	check(has_line(kept.text, "input j_steer `joy.lx`"), "repair mode still converts");
	ps_profile_options_t custom;
	custom.steer_func = 0.5;
	custom.steer_dz = 0.1;
	const ps_profile_result_t tuned = convert_playstation_profile(sii_profile(ds4_field_config(), "\n"), custom);
	check(has_line(tuned.text, "constant c_steer_func 0.500000") && has_line(tuned.text, "constant c_steer_dz 0.100000"), "custom curve values");

	// Unknown names abort without writing.
	std::vector<std::string> odd = ds4_field_config();
	odd.push_back("mix odd `joy.slider1?0`");
	const ps_profile_result_t failed = convert_playstation_profile(sii_profile(odd, "\n"));
	check(failed.error && ! failed.changed && reported(failed, "joy.slider1"), "unknown input aborts");

	// Profiles that must stay as they are.
	unchanged_case({
		"device joy `sdk.padfix`",
		"constant c_steer_func 0.000000", "constant c_steer_dz 0.000000",
		"input j_steer `joy.lx`", "input j_throttle `joy.ry`", "input j_brake `joy.rt`",
		"input j_look_lr `joy.rx`", "input j_look_ud `joy.ry`",
		"mix horn `keyboard.h?0 | joy.x?0`", "mix gear1 `joy.a?0 | semantical.gear1?0`",
	}, "correct padfix profile (right stick throttle, look on rx/ry) untouched");
	unchanged_case({
		"device joy `di8.'{11111111-0000-0000-0000-000000000000}|{028E045E-0000-0000-0000-504944564944}'`",
		"input j_steer `joy.x`", "mix horn `joy.b1?0`",
	}, "Xbox pad on DirectInput untouched");
	unchanged_case({
		"device joy `di8.'{11111111-0000-0000-0000-000000000000}|{C24F046D-0000-0000-0000-504944564944}'`",
		"device joy2 `sdk.padfix`",
		"input j_steer `joy.x`", "input j_throttle `joy.ry`", "mix horn `joy2.a?0`",
	}, "wheel steering next to padfix untouched");
	unchanged_case({
		"device joy `xinput.xinput_gamepad_1`", "device joy2 `sdk.padfix`",
		"input j_steer `joy.x`", "mix steering `joy2.lx?0`",
	}, "broadcast-configured profile untouched");

	// Other Sony models and lower-case GUIDs.
	const char *products[] = { "05C4", "0ce6", "0DF2" };
	for (const char *product : products) {
		const std::string device = std::string("di8.'{1}|{") + product + "054c-0000-0000-0000-504944564944}'";
		check(is_playstation_dinput(device), std::string("Sony product ") + product);
	}
	check(! is_playstation_dinput("di8.'{1}|{0BA0054C-0000-0000-0000-504944564944}'"), "Sony USB wireless adapter not matched");
	check(! is_playstation_dinput("xinput.xinput_gamepad_1"), "XInput not matched");

	printf("PASS: %u checks (PlayStation profile conversion)\n", checks);
	return 0;
}
