#pragma once

#include <windows.h>
#include <xinput.h>

// Keep the strongest input on each axis. Opposing equal magnitudes cancel;
// identical sources are not added together. Treat both full-scale endpoints
// as equally strong even though signed 16-bit values have asymmetric limits.
struct axis_merge_t
{
	int positive = 0;
	int negative = 0;

	void add(const int value)
	{
		if (value > positive) positive = value;
		if (value < negative) negative = value;
	}
	SHORT result() const
	{
		const int negative_strength = negative == -32768 ? 32767 : -negative;
		if (positive == negative_strength) return 0;
		return static_cast<SHORT>(positive > negative_strength ? positive : negative);
	}
};

// Merges any number of sources (XInput slots, PlayStation pads): buttons OR,
// triggers max, sticks per axis as above.
struct pad_merge_t
{
	XINPUT_GAMEPAD pad = {};
	axis_merge_t lx, ly, rx, ry;

	void add(const XINPUT_GAMEPAD &source)
	{
		pad.wButtons |= source.wButtons;
		if (source.bLeftTrigger > pad.bLeftTrigger) pad.bLeftTrigger = source.bLeftTrigger;
		if (source.bRightTrigger > pad.bRightTrigger) pad.bRightTrigger = source.bRightTrigger;
		lx.add(source.sThumbLX);
		ly.add(source.sThumbLY);
		rx.add(source.sThumbRX);
		ry.add(source.sThumbRY);
	}
	XINPUT_GAMEPAD result() const
	{
		XINPUT_GAMEPAD out = pad;
		out.sThumbLX = lx.result();
		out.sThumbLY = ly.result();
		out.sThumbRX = rx.result();
		out.sThumbRY = ry.result();
		return out;
	}
};

inline SHORT merge_stick_axis(const XINPUT_STATE *states, DWORD mask, SHORT XINPUT_GAMEPAD::*axis)
{
	axis_merge_t merge;
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (mask & (1u << i)) merge.add(states[i].Gamepad.*axis);
	}
	return merge.result();
}

inline void add_gamepads(pad_merge_t &merge, const XINPUT_STATE *states, DWORD mask)
{
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (mask & (1u << i)) merge.add(states[i].Gamepad);
	}
}

inline XINPUT_GAMEPAD merge_gamepads(const XINPUT_STATE *states, DWORD mask)
{
	pad_merge_t merge;
	add_gamepads(merge, states, mask);
	return merge.result();
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
