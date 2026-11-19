// Regression checks for PlayStation pads: report parsing, rumble reports,
// rumble resend policy and merging with XInput sources. No hardware access.
#include <stdio.h>
#include <stdlib.h>
#include "../src/playstation_pad.h"
#include "../src/controller_merge.h"

unsigned checks = 0;
void check(bool ok, const char *message)
{
	++checks;
	if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

struct report_case_t { const char *name; ps_model_t model; bool bluetooth; BYTE id; ps_layout_t layout; size_t size; };

void build_report(BYTE *report, const report_case_t &c, BYTE lx, BYTE ly, BYTE rx, BYTE ry, BYTE face, BYTE shoulder, BYTE l2, BYTE r2)
{
	memset(report, 0xA5, c.size); // Unrelated bytes must not leak into the result.
	report[0] = c.id;
	report[c.layout.sticks] = lx;
	report[c.layout.sticks + 1] = ly;
	report[c.layout.sticks + 2] = rx;
	report[c.layout.sticks + 3] = ry;
	report[c.layout.face] = face;
	report[c.layout.shoulder] = shoulder;
	report[c.layout.left_trigger] = l2;
	report[c.layout.right_trigger] = r2;
}

// The merge rules before pad_merge_t, kept here as an independent reference.
SHORT reference_axis(const XINPUT_STATE *states, DWORD mask, SHORT XINPUT_GAMEPAD::*axis)
{
	int positive = 0, negative = 0;
	for (DWORD i = 0; i < 4; ++i) {
		if (!(mask & (1u << i))) continue;
		const int value = states[i].Gamepad.*axis;
		if (value > positive) positive = value;
		if (value < negative) negative = value;
	}
	const int negative_strength = negative == -32768 ? 32767 : -negative;
	if (positive == negative_strength) return 0;
	return static_cast<SHORT>(positive > negative_strength ? positive : negative);
}

bool crc_sealed(const BYTE *report, size_t size)
{
	BYTE whole[PS_RUMBLE_REPORT_MAX + 1];
	whole[0] = 0xA2;
	memcpy(whole + 1, report, size - 4);
	const DWORD crc = ps_crc32(0, whole, size - 3);
	return report[size - 4] == static_cast<BYTE>(crc) && report[size - 3] == static_cast<BYTE>(crc >> 8)
		&& report[size - 2] == static_cast<BYTE>(crc >> 16) && report[size - 1] == static_cast<BYTE>(crc >> 24);
}

int main()
{
	check(ps_model(0x054C, 0x05C4) == PS_DUALSHOCK4, "DualShock 4 v1");
	check(ps_model(0x054C, 0x09CC) == PS_DUALSHOCK4, "DualShock 4 v2");
	check(ps_model(0x054C, 0x0CE6) == PS_DUALSENSE, "DualSense");
	check(ps_model(0x054C, 0x0DF2) == PS_DUALSENSE, "DualSense Edge");
	check(ps_model(0x054C, 0x0BA0) == PS_NONE, "wireless adapter not claimed");
	check(ps_model(0x045E, 0x09CC) == PS_NONE, "other vendors ignored");

	check(ps_axis(128) == 0, "stick rest is exactly zero");
	check(ps_axis(0) == -32768 && ps_axis(255) == 32767, "stick ends reach full scale");
	check(ps_axis(129) > 0 && ps_axis(127) < 0 && ps_axis(129) < 300 && ps_axis(127) > -300, "small deflections stay small");
	bool monotonic = true;
	for (int v = 0; v < 255; ++v) monotonic = monotonic && ps_axis(static_cast<BYTE>(v)) < ps_axis(static_cast<BYTE>(v + 1));
	check(monotonic, "stick mapping strictly increasing");
	check(ps_axis_inverted(0) == 32767 && ps_axis_inverted(255) == -32767 && ps_axis_inverted(128) == 0, "Y grows upwards like XInput");

	const WORD hats[9] = {
		XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT,
		XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_DOWN,
		XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT,
		XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT, 0,
	};
	for (BYTE hat = 0; hat < 9; ++hat) check(ps_buttons(hat, 0) == hats[hat], "D-pad hat direction");
	for (BYTE hat = 9; hat < 16; ++hat) check(ps_buttons(hat, 0) == 0, "invalid hat values press nothing");
	check(ps_buttons(0x18, 0) == XINPUT_GAMEPAD_X, "square is X");
	check(ps_buttons(0x28, 0) == XINPUT_GAMEPAD_A, "cross is A");
	check(ps_buttons(0x48, 0) == XINPUT_GAMEPAD_B, "circle is B");
	check(ps_buttons(0x88, 0) == XINPUT_GAMEPAD_Y, "triangle is Y");
	check(ps_buttons(8, 0x01) == XINPUT_GAMEPAD_LEFT_SHOULDER && ps_buttons(8, 0x02) == XINPUT_GAMEPAD_RIGHT_SHOULDER, "L1/R1 are LB/RB");
	check(ps_buttons(8, 0x10) == XINPUT_GAMEPAD_BACK && ps_buttons(8, 0x20) == XINPUT_GAMEPAD_START, "Share/Create is Back, Options is Start");
	check(ps_buttons(8, 0x40) == XINPUT_GAMEPAD_LEFT_THUMB && ps_buttons(8, 0x80) == XINPUT_GAMEPAD_RIGHT_THUMB, "L3/R3 are stick clicks");
	check(ps_buttons(8, 0x0C) == 0, "digital L2/R2 bits ignored; analog triggers are used");
	check(ps_buttons(0xF2, 0xF3) == (XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y
		| XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START
		| XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB), "everything at once");

	const report_case_t cases[] = {
		{"DualShock 4 USB", PS_DUALSHOCK4, false, 0x01, PS_LAYOUT_COMPACT, 64},
		{"DualShock 4 Bluetooth basic", PS_DUALSHOCK4, true, 0x01, PS_LAYOUT_COMPACT, 10},
		{"DualShock 4 Bluetooth full", PS_DUALSHOCK4, true, 0x11, PS_LAYOUT_DS4_BT, 78},
		{"DualSense USB", PS_DUALSENSE, false, 0x01, PS_LAYOUT_DS5, 64},
		{"DualSense Bluetooth basic", PS_DUALSENSE, true, 0x01, PS_LAYOUT_COMPACT, 10},
		{"DualSense Bluetooth full", PS_DUALSENSE, true, 0x31, PS_LAYOUT_DS5_BT, 78},
	};
	for (const report_case_t &c : cases) {
		BYTE report[80];
		build_report(report, c, 255, 0, 0, 255, 0x24, 0x31, 200, 17);
		XINPUT_GAMEPAD pad = {};
		check(ps_parse_report(c.model, c.bluetooth, report, c.size, pad), c.name);
		check(pad.sThumbLX == 32767 && pad.sThumbLY == 32767, c.name);  // full right, full up
		check(pad.sThumbRX == -32768 && pad.sThumbRY == -32767, c.name); // full left, full down
		check(pad.wButtons == (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START), c.name);
		check(pad.bLeftTrigger == 200 && pad.bRightTrigger == 17, c.name);
		build_report(report, c, 128, 128, 128, 128, 0x08, 0, 0, 0);
		check(ps_parse_report(c.model, c.bluetooth, report, c.size, pad), c.name);
		check(pad.sThumbLX == 0 && pad.sThumbLY == 0 && pad.sThumbRX == 0 && pad.sThumbRY == 0 && pad.wButtons == 0
			&& pad.bLeftTrigger == 0 && pad.bRightTrigger == 0, "controller at rest is neutral");
		size_t last = c.layout.sticks + 3;
		if (c.layout.face > last) last = c.layout.face;
		if (c.layout.shoulder > last) last = c.layout.shoulder;
		if (c.layout.left_trigger > last) last = c.layout.left_trigger;
		if (c.layout.right_trigger > last) last = c.layout.right_trigger;
		pad.wButtons = 0x1234;
		check(!ps_parse_report(c.model, c.bluetooth, report, last, pad) && pad.wButtons == 0x1234, "truncated report rejected, previous state kept");
	}
	BYTE other[78] = {0x05};
	XINPUT_GAMEPAD untouched = {};
	check(!ps_parse_report(PS_DUALSHOCK4, false, other, sizeof(other), untouched), "unknown report IDs ignored");
	other[0] = 0x31;
	check(!ps_parse_report(PS_DUALSHOCK4, true, other, sizeof(other), untouched), "DualSense report not parsed as DualShock 4");
	other[0] = 0x11;
	check(!ps_parse_report(PS_DUALSENSE, true, other, sizeof(other), untouched), "DualShock 4 report not parsed as DualSense");
	other[0] = 0x01;
	check(!ps_parse_report(PS_NONE, false, other, sizeof(other), untouched), "unsupported model ignored");
	check(!ps_parse_report(PS_DUALSHOCK4, false, NULL, 64, untouched), "null report ignored");

	const BYTE digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	check(ps_crc32(0, digits, sizeof(digits)) == 0xCBF43926, "CRC-32 check value");
	check(ps_crc32(ps_crc32(0, digits, 4), digits + 4, 5) == 0xCBF43926, "CRC-32 continues across chunks");

	BYTE out[PS_RUMBLE_REPORT_MAX];
	check(ps_rumble_report(PS_DUALSHOCK4, false, 0xFFFF, 0x8000, 0, out, sizeof(out)) == 32, "DualShock 4 USB report size");
	check(out[0] == 0x05 && out[1] == 0x01 && out[4] == 0x80 && out[5] == 0xFF, "DualShock 4 USB: motors only, weak then strong");
	bool quiet = true;
	for (size_t i = 0; i < 32; ++i) if (i != 0 && i != 1 && i != 4 && i != 5) quiet = quiet && out[i] == 0;
	check(quiet, "DualShock 4 USB lightbar and flash untouched");
	check(ps_rumble_report(PS_DUALSHOCK4, true, 0x4000, 0x0100, 0, out, sizeof(out)) == 78, "DualShock 4 Bluetooth report size");
	check(out[0] == 0x11 && out[1] == 0xC4 && out[3] == 0x01 && out[6] == 0x01 && out[7] == 0x40, "DualShock 4 Bluetooth layout");
	check(crc_sealed(out, 78), "DualShock 4 Bluetooth CRC");
	check(ps_rumble_report(PS_DUALSENSE, false, 0x2000, 0xFF00, 9, out, sizeof(out)) == 48, "DualSense USB report size");
	check(out[0] == 0x02 && out[1] == 0x03 && out[2] == 0 && out[3] == 0xFF && out[4] == 0x20 && out[39] == 0x04, "DualSense USB layout");
	quiet = true;
	for (size_t i = 40; i < 48; ++i) quiet = quiet && out[i] == 0;
	check(quiet, "DualSense USB lightbar and player LEDs untouched");
	check(ps_rumble_report(PS_DUALSENSE, true, 0x2000, 0xFF00, 5, out, sizeof(out)) == 78, "DualSense Bluetooth report size");
	check(out[0] == 0x31 && out[1] == 0x50 && out[2] == 0x10 && out[3] == 0x03 && out[5] == 0xFF && out[6] == 0x20 && out[41] == 0x04, "DualSense Bluetooth layout");
	check(crc_sealed(out, 78), "DualSense Bluetooth CRC");
	ps_rumble_report(PS_DUALSENSE, true, 0, 0, 17, out, sizeof(out));
	check(out[1] == 0x10 && out[5] == 0 && out[6] == 0 && crc_sealed(out, 78), "sequence wraps in the high nibble; stop is zero");
	check(ps_rumble_report(PS_DUALSHOCK4, false, 1, 1, 0, out, 31) == 0, "small buffer refused");
	check(ps_rumble_report(PS_NONE, false, 1, 1, 0, out, sizeof(out)) == 0, "unsupported model has no report");

	ps_rumble_state_t rumble;
	check(rumble.due(0, 0, 0), "motors cleared once after connecting");
	rumble.sent(0, 0, 0, true);
	check(!rumble.due(1, 0, 0), "unchanged command not resent");
	check(rumble.due(2, 30000, 0), "new command sent");
	rumble.sent(2, 30000, 0, false);
	check(!rumble.due(100, 30000, 0) && !rumble.due(100, 0, 0), "failed write backs off");
	check(rumble.due(502, 30000, 0), "failed write retried after 500 ms");
	rumble.sent(502, 30000, 0, true);
	check(!rumble.due(503, 30000, 0) && rumble.due(503, 0, 0), "stop is sent after a successful command");
	ps_rumble_state_t wrap;
	wrap.sent(0xFFFFFF00, 1, 1, false);
	check(!wrap.due(0xFFFFFF10, 1, 1) && wrap.due(0x00000100, 1, 1), "retry delay across timer wrap");

	XINPUT_STATE xinput[4] = {};
	xinput[1].Gamepad.wButtons = XINPUT_GAMEPAD_B;
	xinput[1].Gamepad.sThumbRX = -20000;
	BYTE report[64];
	build_report(report, cases[0], 255, 128, 128, 128, 0x28, 0, 200, 0);
	XINPUT_GAMEPAD playstation = {};
	ps_parse_report(PS_DUALSHOCK4, false, report, sizeof(report), playstation);
	pad_merge_t merge;
	add_gamepads(merge, xinput, 2);
	merge.add(playstation);
	const XINPUT_GAMEPAD merged = merge.result();
	check(merged.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "PlayStation and XInput buttons combined");
	check(merged.sThumbLX == 32767 && merged.sThumbRX == -20000 && merged.bLeftTrigger == 200, "PlayStation steering plus XInput camera simultaneous");
	xinput[1].Gamepad.sThumbLX = -32768;
	pad_merge_t opposing;
	add_gamepads(opposing, xinput, 2);
	opposing.add(playstation);
	check(opposing.result().sThumbLX == 0, "PlayStation pads follow the opposing-direction rule");
	pad_merge_t twice;
	twice.add(playstation);
	twice.add(playstation);
	check(twice.result().sThumbLX == 32767 && twice.result().bLeftTrigger == 200, "same pad read twice (HID and a virtual copy) never doubles");

	unsigned seed = 12345;
	bool same = true;
	for (int round = 0; round < 2000; ++round) {
		XINPUT_STATE states[4] = {};
		for (XINPUT_STATE &state : states) {
			seed = seed * 1103515245u + 12345u;
			state.Gamepad.sThumbLX = static_cast<SHORT>(seed >> 8);
			seed = seed * 1103515245u + 12345u;
			// Include the asymmetric -32768 endpoint often.
			state.Gamepad.sThumbLY = ((seed >> 8) & 0x8001) ? static_cast<SHORT>(-32768) : static_cast<SHORT>(seed >> 12);
			seed = seed * 1103515245u + 12345u;
			state.Gamepad.sThumbRX = static_cast<SHORT>(seed >> 10);
			state.Gamepad.sThumbRY = static_cast<SHORT>(-static_cast<int>(seed >> 18));
			state.Gamepad.wButtons = static_cast<WORD>(seed);
			state.Gamepad.bLeftTrigger = static_cast<BYTE>(seed >> 3);
			state.Gamepad.bRightTrigger = static_cast<BYTE>(seed >> 11);
		}
		const DWORD mask = static_cast<DWORD>(round & 15);
		const XINPUT_GAMEPAD result = merge_gamepads(states, mask);
		same = same && result.sThumbLX == reference_axis(states, mask, &XINPUT_GAMEPAD::sThumbLX)
			&& result.sThumbLY == reference_axis(states, mask, &XINPUT_GAMEPAD::sThumbLY)
			&& result.sThumbRX == reference_axis(states, mask, &XINPUT_GAMEPAD::sThumbRX)
			&& result.sThumbRY == reference_axis(states, mask, &XINPUT_GAMEPAD::sThumbRY)
			&& result.sThumbLX == merge_stick_axis(states, mask, &XINPUT_GAMEPAD::sThumbLX);
	}
	check(same, "refactored merge matches the original rules");

	printf("PASS: %u PlayStation checks (DualShock 4/DualSense USB+Bluetooth reports, rumble reports, resend policy, merge)\n", checks);
	return 0;
}
