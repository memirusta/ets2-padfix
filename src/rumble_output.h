#pragma once
#include <windows.h>
#include <xinput.h>
#include "rumble_shared.h"

// Pure routing policy. Hardware access is injected for regression tests.
struct rumble_output_t {
	typedef DWORD (WINAPI *send_t)(DWORD, XINPUT_VIBRATION *);
	DWORD last_command = 0, touched_slots = 0, inhibited_command = 0;
	DWORD writes = 0, failed_slots = 0;
	bool inhibited = true;
	XINPUT_VIBRATION output = {};

	void send_mask(DWORD mask, XINPUT_VIBRATION value, send_t send)
	{
		for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
			if (mask & (1u << i)) {
				if (send(i, &value) == ERROR_SUCCESS) failed_slots &= ~(1u << i);
				else failed_slots |= 1u << i;
				++writes;
			}
		}
	}
	void stop(send_t send)
	{
		if (touched_slots) send_mask(touched_slots, {}, send);
		touched_slots = 0;
		output = {};
		inhibited = true;
	}
	void poll(DWORD now, DWORD targets, const rumble_command_t &command, DWORD game_pid, bool foreground, send_t send = XInputSetState)
	{
		targets &= 0xf;
		const bool valid = command.magic == PADFIX_RUMBLE_MAGIC && command.game_pid == game_pid
			&& command.enabled && command.command_id && static_cast<DWORD>(now - command.alive_ms) < 1000 && foreground;
		if (!valid) {
			stop(send);
			inhibited_command = command.command_id;
			return;
		}
		// Never replay a command held through loss of focus, a pause/disable or
		// a dead producer. The game must issue a fresh command to resume.
		if (inhibited && command.command_id == inhibited_command) return;
		inhibited = false;
		if (command.command_id != last_command || targets != touched_slots) {
			if (touched_slots & ~targets) send_mask(touched_slots & ~targets, {}, send);
			output = {command.left_motor, command.right_motor};
			send_mask(targets, output, send);
			touched_slots = targets;
			last_command = command.command_id;
		}
	}
};
