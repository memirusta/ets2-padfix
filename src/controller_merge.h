#pragma once

#include <windows.h>
#include <xinput.h>

// Keep the strongest input on each axis. Opposing equal magnitudes cancel;
// identical sources are not added together. Treat both full-scale endpoints
// as equally strong even though signed 16-bit values have asymmetric limits.
inline SHORT merge_stick_axis(const XINPUT_STATE *states, DWORD mask, SHORT XINPUT_GAMEPAD::*axis)
{
	int positive = 0;
	int negative = 0;
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (!(mask & (1u << i))) continue;
		const int value = states[i].Gamepad.*axis;
		if (value > positive) positive = value;
		if (value < negative) negative = value;
	}
	const int negative_strength = negative == -32768 ? 32767 : -negative;
	if (positive == negative_strength) return 0;
	return static_cast<SHORT>(positive > negative_strength ? positive : negative);
}

inline XINPUT_GAMEPAD merge_gamepads(const XINPUT_STATE *states, DWORD mask)
{
	XINPUT_GAMEPAD pad = {};
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (!(mask & (1u << i))) continue;
		const XINPUT_GAMEPAD &source = states[i].Gamepad;
		pad.wButtons |= source.wButtons;
		if (source.bLeftTrigger > pad.bLeftTrigger) pad.bLeftTrigger = source.bLeftTrigger;
		if (source.bRightTrigger > pad.bRightTrigger) pad.bRightTrigger = source.bRightTrigger;
	}
	pad.sThumbLX = merge_stick_axis(states, mask, &XINPUT_GAMEPAD::sThumbLX);
	pad.sThumbLY = merge_stick_axis(states, mask, &XINPUT_GAMEPAD::sThumbLY);
	pad.sThumbRX = merge_stick_axis(states, mask, &XINPUT_GAMEPAD::sThumbRX);
	pad.sThumbRY = merge_stick_axis(states, mask, &XINPUT_GAMEPAD::sThumbRY);
	return pad;
}

struct pad_aggregator_t
{
	typedef DWORD (WINAPI *read_state_t)(DWORD, XINPUT_STATE *);
	XINPUT_STATE states[XUSER_MAX_COUNT] = {};
	DWORD connected_slots = 0;
	DWORD next_scan = 0;
	bool scanned = false;

	void poll(const DWORD now, read_state_t read_state = XInputGetState)
	{
		const bool scan = !scanned || static_cast<LONG>(now - next_scan) >= 0;
		if (scan) {
			scanned = true;
			next_scan = now + 500;
		}
		for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
			const DWORD bit = 1u << i;
			if (!(connected_slots & bit) && !scan) continue;
			XINPUT_STATE sample = {};
			if (read_state(i, &sample) == ERROR_SUCCESS) {
				connected_slots |= bit;
				states[i] = sample;
			}
			else {
				connected_slots &= ~bit;
				states[i] = {};
			}
		}
	}

	XINPUT_GAMEPAD gamepad() const { return merge_gamepads(states, connected_slots); }
};
