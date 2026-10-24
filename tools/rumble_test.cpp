#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <vector>
#include "../src/rumble_bridge.h"
#include "../src/rumble_output.h"
#include "../src/controller_sources.h"
#pragma comment(lib, "xinput.lib")

unsigned checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); } }
struct sent_t { DWORD slot; XINPUT_VIBRATION value; };
std::vector<sent_t> sent;
DWORD WINAPI fake_send(DWORD slot, XINPUT_VIBRATION *value) { sent.push_back({slot, *value}); return ERROR_SUCCESS; }

DWORD WINAPI failing_send(DWORD slot, XINPUT_VIBRATION *value) { fake_send(slot, value); return slot == 2 ? ERROR_DEVICE_NOT_CONNECTED : ERROR_SUCCESS; }

void routing_tests()
{
	rumble_command_t cmd = {};
	cmd.magic = PADFIX_RUMBLE_MAGIC; cmd.game_pid = 42; cmd.enabled = 1;
	cmd.alive_ms = 100; cmd.command_id = 1; cmd.left_motor = 12000; cmd.right_motor = 33000;
	rumble_output_t route;
	route.poll(100, 6, cmd, 42, true, fake_send);
	check(sent.size() == 2 && sent[0].slot == 1 && sent[1].slot == 2, "native rumble broadcast to two physical targets, virtual slot excluded");
	check(sent[0].value.wLeftMotorSpeed == 12000 && sent[1].value.wRightMotorSpeed == 33000, "both motors forwarded unchanged");
	sent.clear();
	route.poll(102, 6, cmd, 42, true, fake_send);
	check(sent.empty(), "unchanged command avoids duplicate output");
	route.poll(104, 10, cmd, 42, true, fake_send);
	check(sent.size() == 3 && sent[0].slot == 2 && sent[0].value.wLeftMotorSpeed == 0 && sent[2].slot == 3, "hotplug removes old target and includes new slot without rebinding");
	sent.clear();
	route.poll(106, 10, cmd, 42, false, fake_send);
	check(sent.size() == 2 && sent[0].value.wRightMotorSpeed == 0 && route.inhibited, "focus loss stops all touched motors");
	sent.clear();
	route.poll(108, 10, cmd, 42, true, fake_send);
	check(sent.empty(), "focus regain cannot replay stale vibration");
	++cmd.command_id;
	route.poll(110, 10, cmd, 42, true, fake_send);
	check(sent.size() == 2 && !route.inhibited, "fresh native command resumes output");
	sent.clear();
	route.poll(1100, 10, cmd, 42, true, fake_send);
	check(sent.size() == 2 && sent[0].value.wLeftMotorSpeed == 0, "producer watchdog stops vibration");
	sent.clear();
	cmd.alive_ms = 1102;
	route.poll(1102, 10, cmd, 42, true, fake_send);
	check(sent.empty(), "heartbeat recovery alone cannot replay stale command");
	++cmd.command_id;
	route.poll(1104, 10, cmd, 42, true, fake_send);
	check(sent.size() == 2, "new command recovers from watchdog");
	sent.clear(); cmd.enabled = 0;
	route.poll(1106, 10, cmd, 42, true, fake_send);
	check(sent.size() == 2 && sent[0].value.wRightMotorSpeed == 0, "pause/disable sends stop");
	sent.clear(); cmd.enabled = 1; ++cmd.command_id;
	route.poll(1108, 10, cmd, 42, true, fake_send);
	route.stop(fake_send);
	check(sent.size() == 4 && sent[3].value.wLeftMotorSpeed == 0, "helper shutdown stops both targets");
	sent.clear(); ++cmd.command_id; cmd.game_pid = 77;
	route.poll(1110, 15, cmd, 42, true, fake_send);
	check(sent.empty(), "commands from another session rejected");
	cmd.game_pid = 42; cmd.magic = 0;
	route.poll(1112, 15, cmd, 42, true, fake_send);
	check(sent.empty(), "uninitialized producer rejected");
	cmd.magic = PADFIX_RUMBLE_MAGIC; ++cmd.command_id; cmd.alive_ms = 0xfffffff0;
	route.poll(0x10, 15, cmd, 42, true, fake_send);
	check(sent.size() == 4, "lease arithmetic survives tick wrap and all four targets");
	++cmd.command_id;
	route.poll(0x12, 15, cmd, 42, true, failing_send);
	check(route.failed_slots == 4, "disconnected/write-failing target recorded without preventing other pads");
	++cmd.command_id;
	route.poll(0x14, 15, cmd, 42, true, fake_send);
	check(route.failed_slots == 0, "next fresh command recovers a reconnected target");
	const std::vector<pad_identity_t> physical_and_virtual = {{0x45e, 0x28e, PAD_VIRTUAL}, {0x45e, 0x28e, PAD_PHYSICAL}};
	check(match_pad_source(0x45e, 0x28e, physical_and_virtual) == PAD_UNKNOWN, "Xbox and virtual identical VID/PID remains ambiguous, real pad is not excluded");
	check(match_pad_source(1, 2, {{1, 2, PAD_VIRTUAL}}) == PAD_VIRTUAL, "known root-backed virtual pad recognized");
}

void bridge_tests()
{
	check(!rumble_bridge_start(nullptr), "bridge rejects missing input state");
	padfix_state_t input = {};
	input.magic = PADFIX_MAGIC; input.connected = 1; input.connected_slots = 3;
	input.heartbeat = 19; input.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
	input.thumb_lx = 12345; input.thumb_rx = -32768; input.right_trigger = 200;
	if (!rumble_bridge_start(&input)) fprintf(stderr, "bridge error: %s\n", rumble_bridge_error());
	check(rumble_bridge_start(&input), "real XInput entrypoint hooks installed and start is idempotent");
	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, PADFIX_RUMBLE_SHM_NAME);
	check(mapping != nullptr, "isolated command channel exists");
	auto shared = static_cast<const rumble_shared_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(rumble_shared_t)));
	check(shared != nullptr, "read isolated command channel");
	check(shared->command.game_pid == GetCurrentProcessId(), "producer belongs to this process");
	XINPUT_STATE state = {};
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == 0, "stable anchor connected before helper input is ready");
	rumble_bridge_tick(true);
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == input.buttons, "stable anchor exposes simultaneous merged buttons");
	check(state.Gamepad.sThumbLX == 12345 && state.Gamepad.sThumbRX == -32768 && state.Gamepad.bRightTrigger == 200, "merged analog values retained through real hooked API");
	input.seq = 1; input.buttons = XINPUT_GAMEPAD_Y;
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "in-progress helper write retains last coherent input instead of twitching to neutral");
	input.seq = 2;
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == XINPUT_GAMEPAD_Y, "coherent helper update replaces cached input");
	input.connected = 0;
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == 0, "unplugging every real controller keeps the anchor present and neutral");
	input.connected = 1; input.connected_slots = 8;
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == input.buttons, "reconnect in another slot needs no native steering rebind");
	XINPUT_CAPABILITIES caps = {};
	check(XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS && (caps.Flags & XINPUT_CAPS_FFB_SUPPORTED), "stable anchor advertises native rumble");
	XINPUT_VIBRATION motors = {123, 456};
	check(XInputSetState(0, &motors) == ERROR_SUCCESS && shared->command.left_motor == 0, "paused session ignores nonzero motor request");
	rumble_bridge_set_driving(true);
	check(XInputSetState(0, &motors) == ERROR_SUCCESS && shared->command.left_motor == 123 && shared->command.right_motor == 456, "native command captured exactly without hardware output");
	const uint32_t command = shared->command.command_id;
	check(XInputSetState(2, &motors) == ERROR_SUCCESS && shared->command.command_id == command, "non-anchor cleanup cannot overwrite broadcast command");
	const auto enable_input = reinterpret_cast<void (WINAPI *)(BOOL)>(GetProcAddress(GetModuleHandleW(L"xinput1_4.dll"), "XInputEnable"));
	enable_input(FALSE);
	check(!shared->command.enabled && shared->command.left_motor == 0, "XInput disable publishes zero and disables output");
	rumble_bridge_tick(true);
	check(XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons == 0, "input heartbeat cannot override XInput disable");
	enable_input(TRUE);
	check(shared->command.enabled && shared->command.left_motor == 0, "reenable starts with zero output");
	XInputSetState(0, &motors);
	rumble_bridge_set_driving(false);
	check(!shared->command.enabled && shared->command.left_motor == 0, "menu/pause event immediately clears vibration");
	check(XInputGetState(0, nullptr) == ERROR_BAD_ARGUMENTS && XInputSetState(4, &motors) == ERROR_BAD_ARGUMENTS, "invalid arguments rejected");
	rumble_bridge_stop();
	check(!shared->command.enabled && shared->command.right_motor == 0, "shutdown leaves neutral output for external helper");
	UnmapViewOfFile(shared); CloseHandle(mapping);
	check(rumble_bridge_start(&input), "hooks can restart after clean removal");
	rumble_bridge_stop();
}

int main()
{
	routing_tests(); bridge_tests();
	printf("PASS: %u rumble checks (broadcast, hotplug, watchdog, focus, pause, real API hooks); no motor writes\n", checks);
	return 0;
}
