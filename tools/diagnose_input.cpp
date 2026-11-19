// Live merge check. Does not start a helper or touch game settings.
// Shows every XInput slot and every PlayStation pad read over HID, and their merge.
// diagnose_input.exe [seconds] [--rumble]
//   --rumble: PlayStation pads vibrate gently while Cross is held (checks the
//             rumble path); without it nothing is ever written to a controller.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/playstation_hid.h"
#pragma comment(lib, "xinput.lib")

bool active(const XINPUT_GAMEPAD &p)
{
	return p.wButtons || p.bLeftTrigger > 30 || p.bRightTrigger > 30
		|| abs(p.sThumbLX) > 8000 || abs(p.sThumbLY) > 8000
		|| abs(p.sThumbRX) > 8000 || abs(p.sThumbRY) > 8000;
}

void print_pad(const char *label, unsigned index, const XINPUT_GAMEPAD &p)
{
	printf("  %s %u: buttons=0x%04X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d\n", label, index,
		p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY);
}

int main(int argc, char **argv)
{
	const int seconds = argc > 1 ? atoi(argv[1]) : 20;
	if (seconds < 1 || seconds > 60) return 1;
	const bool rumble_check = argc > 2 && strcmp(argv[2], "--rumble") == 0;
	if (rumble_check) printf("rumble check: hold Cross on a PlayStation pad to make it vibrate\n");
	pad_aggregator_t pads;
	ps_pads_t playstation;
	DWORD previous_mask = 0xffffffff;
	DWORD previous_ps = 0xffffffff;
	size_t previous_ps_count = static_cast<size_t>(-1);
	unsigned active_samples[4] = {};
	unsigned ps_active_samples[PS_MAX_PADS] = {};
	unsigned simultaneous = 0;
	XINPUT_GAMEPAD previous = {};
	ULONGLONG next_report = 0;
	const ULONGLONG end = GetTickCount64() + seconds * 1000;
	while (GetTickCount64() < end) {
		const DWORD tick = GetTickCount();
		pads.poll(tick);
		playstation.poll(tick);
		const DWORD ps_mask = playstation.mask(tick);
		pad_merge_t merge;
		add_gamepads(merge, pads.states, pads.connected_slots);
		playstation.add_to(merge, tick);
		const XINPUT_GAMEPAD merged = merge.result();
		if (pads.connected_slots != previous_mask) {
			printf("connected XInput slots mask: 0x%lX (bits 0-3 = slots 1-4), all merged\n", pads.connected_slots);
			previous_mask = pads.connected_slots;
			fflush(stdout);
		}
		if (ps_mask != previous_ps || playstation.devices.size() != previous_ps_count) {
			printf("PlayStation pads: %zu open, reporting mask 0x%lX\n", playstation.devices.size(), ps_mask);
			for (size_t i = 0; i < playstation.devices.size(); ++i) {
				const ps_device_t &device = *playstation.devices[i];
				printf("  pad %zu: %s over %s%s%s\n", i + 1, ps_model_name(device.model), device.bluetooth ? "Bluetooth" : "USB",
					device.writable ? "" : " (read-only: rumble unavailable)", device.fresh(tick) ? "" : " (no reports yet)");
			}
			previous_ps = ps_mask;
			previous_ps_count = playstation.devices.size();
			fflush(stdout);
		}
		unsigned count = 0;
		for (DWORD i = 0; i < 4; ++i) {
			if ((pads.connected_slots & (1u << i)) && active(pads.states[i].Gamepad)) { ++active_samples[i]; ++count; }
		}
		for (size_t i = 0; i < playstation.devices.size() && i < PS_MAX_PADS; ++i) {
			if (playstation.devices[i]->fresh(tick) && active(playstation.devices[i]->state)) { ++ps_active_samples[i]; ++count; }
		}
		if (count > 1) ++simultaneous;
		if (rumble_check) {
			bool cross = false;
			for (const auto &device : playstation.devices) {
				cross = cross || (device->fresh(tick) && (device->state.wButtons & XINPUT_GAMEPAD_A));
			}
			playstation.rumble(tick, cross ? 0x6000 : 0, cross ? 0x3000 : 0);
		}
		const ULONGLONG now = GetTickCount64();
		if (now >= next_report && memcmp(&previous, &merged, sizeof(merged)) != 0) {
			printf("merged: buttons=0x%04X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d\n", merged.wButtons,
				merged.bLeftTrigger, merged.bRightTrigger, merged.sThumbLX, merged.sThumbLY, merged.sThumbRX, merged.sThumbRY);
			for (DWORD i = 0; i < 4; ++i) {
				if (pads.connected_slots & (1u << i)) print_pad("slot", i + 1, pads.states[i].Gamepad);
			}
			for (size_t i = 0; i < playstation.devices.size(); ++i) {
				if (playstation.devices[i]->fresh(tick)) print_pad("PlayStation pad", static_cast<unsigned>(i + 1), playstation.devices[i]->state);
			}
			previous = merged;
			next_report = now + 200;
			fflush(stdout);
		}
		Sleep(2);
	}
	if (rumble_check) {
		unsigned failed = 0;
		for (const auto &device : playstation.devices) failed += device->rumble.failed ? 1u : 0u;
		printf("rumble writes failing on %u PlayStation pad(s)\n", failed);
	}
	for (DWORD i = 0; i < 4; ++i) printf("slot %lu: active polls %u\n", i + 1, active_samples[i]);
	for (size_t i = 0; i < playstation.devices.size() && i < PS_MAX_PADS; ++i) printf("PlayStation pad %zu: active polls %u\n", i + 1, ps_active_samples[i]);
	printf("polls with simultaneous activity from multiple sources: %u\n", simultaneous);
	playstation.stop(); // Motors off if Cross was still held.
	return 0;
}
