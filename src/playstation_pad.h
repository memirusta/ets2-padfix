#pragma once

// PlayStation controller reports as XInput-style gamepad values. Pure functions
// only; HID access lives in playstation_hid.h so these can be regression tested.
//
// Supported: DualShock 4 (v1, v2) and DualSense (incl. Edge), over USB or Bluetooth.
// Layouts follow the Linux hid-sony / hid-playstation drivers.

#include <windows.h>
#include <xinput.h>
#include <string.h>

const WORD SONY_VENDOR_ID = 0x054C;

enum ps_model_t { PS_NONE, PS_DUALSHOCK4, PS_DUALSENSE };

inline ps_model_t ps_model(const WORD vendor, const WORD product)
{
	if (vendor != SONY_VENDOR_ID) return PS_NONE;
	switch (product) {
		case 0x05C4: // DualShock 4 (CUH-ZCT1)
		case 0x09CC: // DualShock 4 (CUH-ZCT2)
			return PS_DUALSHOCK4;
		case 0x0CE6: // DualSense
		case 0x0DF2: // DualSense Edge
			return PS_DUALSENSE;
		default:
			return PS_NONE;
	}
}

inline const char *ps_model_name(const ps_model_t model)
{
	return model == PS_DUALSHOCK4 ? "DualShock 4" : model == PS_DUALSENSE ? "DualSense" : "unknown";
}

// Stick byte 0..255 with 128 at rest: rest maps to exactly 0, both ends to full scale.
inline SHORT ps_axis(const BYTE value)
{
	const int centered = static_cast<int>(value) - 128;
	return static_cast<SHORT>(centered >= 0 ? centered * 32767 / 127 : centered * 256);
}

// PlayStation stick Y grows downwards, XInput Y grows upwards.
inline SHORT ps_axis_inverted(const BYTE value)
{
	const int inverted = -static_cast<int>(ps_axis(value));
	return static_cast<SHORT>(inverted > 32767 ? 32767 : inverted);
}

// Face byte (D-pad hat in the low nibble, then square/cross/circle/triangle) and
// shoulder byte (L1, R1, L2, R2, Share/Create, Options, L3, R3) are the same on
// both controller families. L2/R2 come from the analog trigger bytes instead.
inline WORD ps_buttons(const BYTE face, const BYTE shoulder)
{
	static const WORD hat[8] = {
		XINPUT_GAMEPAD_DPAD_UP,
		XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
		XINPUT_GAMEPAD_DPAD_RIGHT,
		XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
		XINPUT_GAMEPAD_DPAD_DOWN,
		XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
		XINPUT_GAMEPAD_DPAD_LEFT,
		XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
	};
	WORD buttons = (face & 0x0F) < 8 ? hat[face & 0x0F] : 0; // 8 = released
	if (face & 0x10) buttons |= XINPUT_GAMEPAD_X; // square
	if (face & 0x20) buttons |= XINPUT_GAMEPAD_A; // cross
	if (face & 0x40) buttons |= XINPUT_GAMEPAD_B; // circle
	if (face & 0x80) buttons |= XINPUT_GAMEPAD_Y; // triangle
	if (shoulder & 0x01) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (shoulder & 0x02) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (shoulder & 0x10) buttons |= XINPUT_GAMEPAD_BACK;  // Share / Create
	if (shoulder & 0x20) buttons |= XINPUT_GAMEPAD_START; // Options
	if (shoulder & 0x40) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (shoulder & 0x80) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
	return buttons;
}

// Byte offsets from the report ID. Sticks are lx, ly, rx, ry from `sticks`.
struct ps_layout_t { size_t sticks, face, shoulder, left_trigger, right_trigger; };

// DualShock 4 USB 0x01 and the basic Bluetooth 0x01 report of both families.
const ps_layout_t PS_LAYOUT_COMPACT = {1, 5, 6, 8, 9};
// DualShock 4 Bluetooth full report 0x11: compact layout two bytes later.
const ps_layout_t PS_LAYOUT_DS4_BT = {3, 7, 8, 10, 11};
// DualSense USB 0x01: triggers moved in front of the buttons.
const ps_layout_t PS_LAYOUT_DS5 = {1, 8, 9, 5, 6};
// DualSense Bluetooth full report 0x31: USB layout one byte later.
const ps_layout_t PS_LAYOUT_DS5_BT = {2, 9, 10, 6, 7};

inline bool ps_parse_report(const ps_model_t model, const bool bluetooth, const BYTE *const report, const size_t size,
	XINPUT_GAMEPAD &out)
{
	if (! report || size == 0) return false;
	const ps_layout_t *layout = NULL;
	if (model == PS_DUALSHOCK4) {
		if (report[0] == 0x01) layout = &PS_LAYOUT_COMPACT;
		else if (report[0] == 0x11) layout = &PS_LAYOUT_DS4_BT;
	}
	else if (model == PS_DUALSENSE) {
		if (report[0] == 0x01) layout = bluetooth ? &PS_LAYOUT_COMPACT : &PS_LAYOUT_DS5;
		else if (report[0] == 0x31) layout = &PS_LAYOUT_DS5_BT;
	}
	if (! layout) return false;
	size_t last = layout->sticks + 3;
	if (layout->face > last) last = layout->face;
	if (layout->shoulder > last) last = layout->shoulder;
	if (layout->left_trigger > last) last = layout->left_trigger;
	if (layout->right_trigger > last) last = layout->right_trigger;
	if (size <= last) return false;

	XINPUT_GAMEPAD pad = {};
	pad.sThumbLX = ps_axis(report[layout->sticks]);
	pad.sThumbLY = ps_axis_inverted(report[layout->sticks + 1]);
	pad.sThumbRX = ps_axis(report[layout->sticks + 2]);
	pad.sThumbRY = ps_axis_inverted(report[layout->sticks + 3]);
	pad.wButtons = ps_buttons(report[layout->face], report[layout->shoulder]);
	pad.bLeftTrigger = report[layout->left_trigger];
	pad.bRightTrigger = report[layout->right_trigger];
	out = pad;
	return true;
}

// Standard CRC-32 (IEEE 802.3, as zlib). Bluetooth output reports carry one.
inline DWORD ps_crc32(DWORD crc, const BYTE *data, size_t size)
{
	crc = ~crc;
	while (size--) {
		crc ^= *data++;
		for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// Seals a Bluetooth output report: CRC-32 over the 0xA2 HID header plus all
// bytes but the last four, stored little-endian in the last four.
inline void ps_seal_bluetooth(BYTE *const report, const size_t size)
{
	const BYTE header = 0xA2;
	DWORD crc = ps_crc32(0, &header, 1);
	crc = ps_crc32(crc, report, size - 4);
	report[size - 4] = static_cast<BYTE>(crc);
	report[size - 3] = static_cast<BYTE>(crc >> 8);
	report[size - 2] = static_cast<BYTE>(crc >> 16);
	report[size - 1] = static_cast<BYTE>(crc >> 24);
}

const size_t PS_RUMBLE_REPORT_MAX = 78;

// Builds an output report that only sets the two motors; lightbar, player LEDs
// and audio keep whatever another program set. left = strong low-frequency
// motor, right = weak high-frequency motor, both 0..65535 like XInput.
// `sequence` is used by DualSense Bluetooth reports. Returns the report size.
inline size_t ps_rumble_report(const ps_model_t model, const bool bluetooth, const WORD left, const WORD right,
	const BYTE sequence, BYTE *const out, const size_t capacity)
{
	if (! out || capacity < PS_RUMBLE_REPORT_MAX) return 0;
	memset(out, 0, PS_RUMBLE_REPORT_MAX);
	const BYTE strong = static_cast<BYTE>(left >> 8);
	const BYTE weak = static_cast<BYTE>(right >> 8);
	if (model == PS_DUALSHOCK4) {
		if (! bluetooth) {
			out[0] = 0x05;
			out[1] = 0x01; // motors only
			out[4] = weak;
			out[5] = strong;
			return 32;
		}
		out[0] = 0x11;
		out[1] = 0xC4; // HID + CRC, 4 ms report interval
		out[3] = 0x01; // motors only
		out[6] = weak;
		out[7] = strong;
		ps_seal_bluetooth(out, 78);
		return 78;
	}
	if (model == PS_DUALSENSE) {
		size_t common = 1;
		if (bluetooth) {
			out[0] = 0x31;
			out[1] = static_cast<BYTE>((sequence & 0x0F) << 4);
			out[2] = 0x10; // required tag
			common = 3;
		}
		else {
			out[0] = 0x02;
		}
		out[common + 0] = 0x03;  // compatible vibration, rumble instead of audio haptics
		out[common + 2] = weak;
		out[common + 3] = strong;
		out[common + 38] = 0x04; // improved vibration on firmware 2.24+, ignored before
		if (! bluetooth) return 48;
		ps_seal_bluetooth(out, 78);
		return 78;
	}
	return 0;
}

// When to (re)send motor values to one controller: on change, once after it
// connects (so a stale motor state is cleared), and after a failure only
// every 500 ms.
struct ps_rumble_state_t
{
	bool known = false;
	bool failed = false;
	WORD left = 0, right = 0;
	DWORD retry_at = 0;

	bool due(const DWORD now, const WORD want_left, const WORD want_right) const
	{
		if (known && left == want_left && right == want_right) return false;
		return ! failed || static_cast<LONG>(now - retry_at) >= 0;
	}
	void sent(const DWORD now, const WORD new_left, const WORD new_right, const bool ok)
	{
		failed = ! ok;
		known = ok;
		if (ok) {
			left = new_left;
			right = new_right;
		}
		else {
			retry_at = now + 500;
		}
	}
};
