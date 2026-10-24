// Read-only live merge check. Does not start a helper or touch game settings.
// diagnose_input.exe [seconds]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/controller_merge.h"
#pragma comment(lib, "xinput.lib")

bool active(const XINPUT_GAMEPAD &p)
{
	return p.wButtons || p.bLeftTrigger > 30 || p.bRightTrigger > 30
		|| abs(p.sThumbLX) > 8000 || abs(p.sThumbLY) > 8000
		|| abs(p.sThumbRX) > 8000 || abs(p.sThumbRY) > 8000;
}

int main(int argc, char **argv)
{
	const int seconds = argc > 1 ? atoi(argv[1]) : 20;
	if (seconds < 1 || seconds > 60) return 1;
	pad_aggregator_t pads;
	DWORD previous_mask = 0xffffffff;
	unsigned active_samples[4] = {};
	unsigned simultaneous = 0;
	XINPUT_GAMEPAD previous = {};
	ULONGLONG next_report = 0;
	const ULONGLONG end = GetTickCount64() + seconds * 1000;
	while (GetTickCount64() < end) {
		pads.poll(GetTickCount());
		const XINPUT_GAMEPAD merged = pads.gamepad();
		if (pads.connected_slots != previous_mask) {
			printf("connected slots mask: 0x%lX (bits 0-3 = slots 1-4), all merged\n", pads.connected_slots);
			previous_mask = pads.connected_slots;
			fflush(stdout);
		}
		unsigned count = 0;
		for (DWORD i = 0; i < 4; ++i) {
			if ((pads.connected_slots & (1u << i)) && active(pads.states[i].Gamepad)) { ++active_samples[i]; ++count; }
		}
		if (count > 1) ++simultaneous;
		const ULONGLONG now = GetTickCount64();
		if (now >= next_report && memcmp(&previous, &merged, sizeof(merged)) != 0) {
			printf("merged: buttons=0x%04X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d\n", merged.wButtons,
				merged.bLeftTrigger, merged.bRightTrigger, merged.sThumbLX, merged.sThumbLY, merged.sThumbRX, merged.sThumbRY);
			for (DWORD i = 0; i < 4; ++i) {
				if (!(pads.connected_slots & (1u << i))) continue;
				const auto &p = pads.states[i].Gamepad;
				printf("  slot %lu: buttons=0x%04X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d\n", i + 1,
					p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY);
			}
			previous = merged;
			next_report = now + 200;
			fflush(stdout);
		}
		Sleep(2);
	}
	for (DWORD i = 0; i < 4; ++i) printf("slot %lu: active polls %u\n", i + 1, active_samples[i]);
	printf("polls with simultaneous activity from multiple sources: %u\n", simultaneous);
	return 0;
}
