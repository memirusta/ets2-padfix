// Regression checks for simultaneous input, hotplug, steering and the SDK path.
#include <stdio.h>
#include <algorithm>
#include <math.h>
#include "../src/helper.cpp"
#include "scssdk_input.h"

unsigned checks = 0;
void check(bool ok, const char *message)
{
	++checks;
	if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

bool present[4] = {};
XINPUT_STATE samples[4] = {};
unsigned reads[4] = {};
DWORD WINAPI fake_read(DWORD slot, XINPUT_STATE *state)
{
	++reads[slot];
	*state = samples[slot];
	return present[slot] ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

void steering_case(const std::string &device, const std::string &axis, DWORD slot,
	const std::string &expected_device, const std::string &expected_axis, const char *newline)
{
	wchar_t temp[MAX_PATH], path[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, temp);
	GetTempFileNameW(temp, L"pfx", 0, path);
	const std::string nl = newline;
	const std::string before = "SiiNunit" + nl + "{" + nl
		+ " config_lines[0]: \"device joy `sdk.padfix`\"" + nl
		+ " config_lines[1]: \"device joy2 `" + device + "`\"" + nl
		+ " config_lines[2]: \"input j_steer `" + axis + "`\"" + nl
		+ " config_lines[3]: \"mix horn `joy.a?0`\"" + nl + "}" + nl;
	check(write_file_replace(path, before), "write fixture");
	const int code = fix_steering(path, slot);
	std::string after;
	check(read_file(path, after), "read repaired fixture");
	check(after.find("device joy2 `" + expected_device + "`") != std::string::npos, "correct native device slot");
	check(after.find("input j_steer `" + expected_axis + "`") != std::string::npos, "correct steering alias");
	check(after.find("mix horn `joy.a?0`") != std::string::npos, "other bindings preserved");
	check(detect_newline(after) == nl, "newlines preserved");
	check(code == (after == before ? FIX_NOTHING_TO_DO : FIX_CHANGED), "accurate repair result");
	if (after != before) {
		std::string backup;
		check(read_file((std::wstring(path) + L".padfix.bak").c_str(), backup) && backup == before, "exact backup");
	}
	check(fix_steering(path, slot) == FIX_NOTHING_TO_DO, "repair is idempotent");
	DeleteFileW(path);
	DeleteFileW((std::wstring(path) + L".padfix.bak").c_str());
}

void merged_steering_case(const std::string &device, const std::string &axis,
	const std::string &expected_axis, const char *newline, bool has_padfix = true)
{
	wchar_t temp[MAX_PATH], path[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, temp);
	check(GetTempFileNameW(temp, L"pfx", 0, path) != 0, "create merged steering fixture");
	const std::string nl = newline;
	const std::string before = "SiiNunit" + nl + "{" + nl
		+ " config_lines[0]: \"device joy3 `" + (has_padfix ? "sdk.padfix" : "") + "`\"" + nl
		+ " config_lines[1]: \"device joy2 `" + device + "`\"" + nl
		+ " config_lines[2]: \"input j_steer `" + axis + "`\"" + nl
		+ " config_lines[3]: \"mix horn `joy3.a?0 | joy2.b1?0`\"" + nl + "}" + nl;
	check(write_file_replace(path, before), "write merged steering fixture");
	std::string expected = before;
	expected.replace(expected.find("input j_steer `") + 15, axis.size(), expected_axis);
	const int result = fix_merged_steering(path);
	std::string after;
	check(read_file(path, after), "read merged steering fixture");
	check(after == expected, "only steering axis changes; device declarations and other bindings preserved");
	check(result == (axis == expected_axis ? FIX_NOTHING_TO_DO : FIX_CHANGED), "merged steering result");
	if (result == FIX_CHANGED) {
		std::string backup;
		check(read_file((std::wstring(path) + L".padfix.bak").c_str(), backup) && backup == before, "merged steering exact backup");
	}
	check(fix_merged_steering(path) == FIX_NOTHING_TO_DO, "merged steering idempotent");
	DeleteFileW(path);
	DeleteFileW((std::wstring(path) + L".padfix.bak").c_str());
}

void rumble_case(const std::string &native_device, const std::string &axis,
	const std::string &mix, const std::string &expected_axis, const std::string &expected_mix,
	DWORD slot, int expected_result, const char *newline, bool free_slot = true, bool sdk_present = true)
{
	wchar_t temp[MAX_PATH], path[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, temp);
	check(GetTempFileNameW(temp, L"pfx", 0, path) != 0, "create rumble fixture");
	const std::string nl = newline;
	const std::string before = "SiiNunit" + nl + "{" + nl
		+ " config_lines[0]: \"device joy `" + (sdk_present ? "sdk.padfix" : "") + "`\"" + nl
		+ " config_lines[1]: \"device joy2 `" + native_device + "`\"" + nl
		+ " config_lines[2]: \"device joy4 `" + (free_slot ? "" : "di8.other") + "`\"" + nl
		+ " config_lines[3]: \"input j_steer `" + axis + "`\"" + nl
		+ " config_lines[4]: \"mix steering `" + mix + "`\"" + nl
		+ " config_lines[5]: \"constant c_ff_gain 5.000000\"" + nl
		+ " config_lines[6]: \"mix horn `joy.a?0 | joy2.b1?0`\"" + nl + "}" + nl;
	std::string expected = before;
	if (expected_result == FIX_CHANGED) {
		if (native_device != "xinput.xinput_gamepad_" + std::to_string(slot + 1)) {
			const size_t pos = expected.find("device joy4 ``");
			check(pos != std::string::npos, "test expects free alias");
			expected.insert(pos + 13, "xinput.xinput_gamepad_" + std::to_string(slot + 1));
		}
		expected.replace(expected.find("input j_steer `") + 15, axis.size(), expected_axis);
		expected.replace(expected.find("mix steering `") + 14, mix.size(), expected_mix);
	}
	check(write_file_replace(path, before), "write rumble fixture");
	check(fix_merged_rumble(path, slot) == expected_result, "rumble repair result");
	std::string after;
	check(read_file(path, after) && after == expected, "only intended routing changes; FFB gain and other bindings untouched");
	if (expected_result == FIX_CHANGED) {
		std::string backup;
		check(read_file((std::wstring(path) + L".padfix.bak").c_str(), backup) && backup == before, "rumble exact backup");
		check(fix_merged_rumble(path, slot) == FIX_NOTHING_TO_DO, "rumble repair idempotent");
		std::string unchanged_backup;
		check(read_file((std::wstring(path) + L".padfix.bak").c_str(), unchanged_backup) && unchanged_backup == before, "idempotent repair preserves original backup");
	}
	DeleteFileW(path);
	DeleteFileW((std::wstring(path) + L".padfix.bak").c_str());
}

scs_input_device_t sdk_device;
SCSAPI_VOID sdk_log(const scs_log_type_t, const scs_string_t message) { printf("  %s\n", message); }
SCSAPI_RESULT sdk_register(const scs_input_device_t *device) { sdk_device = *device; return SCS_RESULT_ok; }

void sdk_frame(padfix_state_t *shared, pad_aggregator_t &pads)
{
	publish_state(shared, pads);
	float axes[6] = {};
	bool buttons[24] = {};
	unsigned count = 0;
	scs_input_event_t event = {};
	scs_u32_t flags = SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame;
	while (sdk_device.input_event_callback(&event, flags, sdk_device.callback_context) == SCS_RESULT_ok) {
		flags = 0;
		check(event.input_index < 30, "SDK event index");
		if (event.input_index < 6) axes[event.input_index] = event.value_float.value;
		else buttons[event.input_index - 6] = event.value_bool.value != 0;
		check(++count <= 30, "SDK bounded frame");
	}
	check(count == 30, "SDK complete frame");
	const XINPUT_GAMEPAD pad = pads.gamepad();
	check(buttons[0] == !!(pad.wButtons & XINPUT_GAMEPAD_A), "merged A reaches SDK");
	check(buttons[1] == !!(pad.wButtons & XINPUT_GAMEPAD_B), "merged B reaches SDK");
	check(fabsf(axes[0] - static_cast<float>(pad.sThumbLX) / 32767) < 0.00001f, "HowlControl steering reaches SDK");
	check(fabsf(axes[2] - (pad.sThumbRX == -32768 ? -1.0f : static_cast<float>(pad.sThumbRX) / 32767)) < 0.00001f, "physical camera reaches SDK simultaneously");
	check(fabsf(axes[3] + static_cast<float>(pad.sThumbRY) / 32767) < 0.00001f, "SDK Y direction preserved");
	check(fabsf(axes[4] - static_cast<float>(pad.bLeftTrigger) / 255) < 0.00001f, "merged LT reaches SDK");
	check(fabsf(axes[5] - static_cast<float>(pad.bRightTrigger) / 255) < 0.00001f, "merged RT reaches SDK");
}

void sdk_integration(const char *dll_path)
{
	// Own an isolated, idle v2 session. Refuse to touch a running game's mapping.
	HANDLE mutex = CreateMutexW(nullptr, TRUE, PADFIX_MUTEX_NAME);
	check(mutex && GetLastError() != ERROR_ALREADY_EXISTS, "SDK integration requires game/helper closed");
	HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(padfix_state_t), PADFIX_SHM_NAME);
	check(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "SDK test owns new shared mapping");
	auto shared = static_cast<padfix_state_t *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(padfix_state_t)));
	check(shared != nullptr, "map SDK test state");
	*shared = {};
	shared->magic = PADFIX_MAGIC;
	XINPUT_STATE sources[4] = {};
	sources[0].Gamepad.wButtons = XINPUT_GAMEPAD_A;
	sources[0].Gamepad.sThumbLX = 12345;
	sources[0].Gamepad.bLeftTrigger = 100;
	sources[1].Gamepad.wButtons = XINPUT_GAMEPAD_B;
	sources[1].Gamepad.sThumbRX = -32768;
	sources[1].Gamepad.sThumbRY = 16384;
	sources[1].Gamepad.bRightTrigger = 200;
	pad_aggregator_t pads;
	pads.connected_slots = 3;
	memcpy(pads.states, sources, sizeof(sources));
	publish_state(shared, pads);
	HMODULE dll = LoadLibraryA(dll_path);
	check(dll != nullptr, "load SDK plugin");
	typedef SCSAPI_RESULT_FPTR(init_fn_t)(scs_u32_t, const scs_input_init_params_t *);
	typedef SCSAPI_VOID_FPTR(shutdown_fn_t)(void);
	auto init = reinterpret_cast<init_fn_t>(GetProcAddress(dll, "scs_input_init"));
	auto shutdown = reinterpret_cast<shutdown_fn_t>(GetProcAddress(dll, "scs_input_shutdown"));
	check(init && shutdown, "SDK exports");
	scs_input_init_params_v100_t params = {};
	params.common.game_id = "padfix.merge.integration"; // no real profile path
	params.common.game_name = "padfix merge test";
	params.common.log = sdk_log;
	params.register_device = sdk_register;
	check(init(SCS_INPUT_VERSION_1_00, &params) == SCS_RESULT_ok, "SDK initializes");
	check(sdk_device.input_count == 30, "SDK interface unchanged");
	sdk_frame(shared, pads);
	check(shared->connected_slots == 3, "IPC keeps both contributing slots");
	pads.connected_slots = 1;
	sdk_frame(shared, pads);
	pads.connected_slots = 0;
	sdk_frame(shared, pads);
	shutdown();
	FreeLibrary(dll);
	UnmapViewOfFile(shared);
	CloseHandle(mapping);
	ReleaseMutex(mutex);
	CloseHandle(mutex);
}

int main(int argc, char **argv)
{
	static_assert(sizeof(padfix_state_t) == 32, "shared ABI size");
	pad_aggregator_t pads;
	pads.poll(0, fake_read);
	check(pads.connected_slots == 0 && pads.gamepad().wButtons == 0, "empty system neutral");
	pads.poll(2, fake_read);
	check(reads[0] == 1 && reads[3] == 1, "empty-slot queries throttled");
	present[0] = present[1] = true;
	samples[0].Gamepad.wButtons = XINPUT_GAMEPAD_A;
	samples[0].Gamepad.sThumbLX = 12345;
	samples[0].Gamepad.bLeftTrigger = 100;
	samples[1].Gamepad.wButtons = XINPUT_GAMEPAD_B;
	samples[1].Gamepad.sThumbRX = -32768;
	samples[1].Gamepad.bRightTrigger = 200;
	pads.poll(500, fake_read);
	check(pads.connected_slots == 3, "physical and virtual slots connected together");
	check(pads.gamepad().wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "buttons from both sources combined");
	check(pads.gamepad().sThumbLX == 12345 && pads.gamepad().sThumbRX == -32768, "keyboard steering plus pad camera simultaneous");
	check(pads.gamepad().bLeftTrigger == 100 && pads.gamepad().bRightTrigger == 200, "independent analog pedals simultaneous");
	samples[1].Gamepad.wButtons |= XINPUT_GAMEPAD_A;
	samples[1].Gamepad.bLeftTrigger = 150;
	pads.poll(502, fake_read);
	check(pads.gamepad().bLeftTrigger == 150, "same trigger takes stronger value");
	samples[0].Gamepad.wButtons = 0;
	pads.poll(504, fake_read);
	check((pads.gamepad().wButtons & XINPUT_GAMEPAD_A) != 0, "release by one source cannot release another source's held button");
	present[1] = false;
	pads.poll(506, fake_read);
	check(pads.connected_slots == 1 && pads.gamepad().wButtons == 0, "unplug removes only disconnected buttons");
	check(pads.gamepad().sThumbLX == 12345 && pads.gamepad().sThumbRX == 0 && pads.gamepad().bRightTrigger == 0, "unplug keeps other source and clears disconnected axes");
	check(pads.states[1].Gamepad.wButtons == 0, "failed samples cleared even if driver wrote stale data");
	present[0] = false;
	pads.poll(508, fake_read);
	check(pads.connected_slots == 0 && pads.gamepad().sThumbLX == 0, "all unplugged neutral");
	present[3] = true;
	samples[3].Gamepad.sThumbLY = -32768;
	pads.poll(1000, fake_read);
	check(pads.connected_slots == 8 && pads.gamepad().sThumbLY == -32768, "reconnect at a different slot");
	pad_aggregator_t wrap;
	wrap.poll(0xfffffff0, fake_read);
	const unsigned old_reads = reads[2];
	wrap.poll(0x10, fake_read);
	check(reads[2] == old_reads, "scan delay across timer wrap");
	wrap.poll(0x200, fake_read);
	check(reads[2] == old_reads + 1, "scan resumes after timer wrap");
	XINPUT_STATE conflict[4] = {};
	conflict[0].Gamepad.sThumbLX = 20000;
	conflict[1].Gamepad.sThumbLX = -12000;
	check(merge_gamepads(conflict, 3).sThumbLX == 20000, "stronger opposing direction wins");
	conflict[1].Gamepad.sThumbLX = -20000;
	check(merge_gamepads(conflict, 3).sThumbLX == 0, "equal opposing directions neutral");
	conflict[2].Gamepad.sThumbLX = 20000;
	check(merge_gamepads(conflict, 7).sThumbLX == 0, "third duplicate cannot undo opposing cancellation");
	conflict[3].Gamepad.sThumbLX = -25000;
	check(merge_gamepads(conflict, 15).sThumbLX == -25000, "strongest of all four wins");
	const SHORT expected = -25000;
	int order[] = {0, 1, 2, 3};
	do {
		XINPUT_STATE reordered[4] = {};
		for (unsigned i = 0; i < 4; ++i) reordered[i] = conflict[order[i]];
		check(merge_gamepads(reordered, 15).sThumbLX == expected, "axis merge independent of slot ordering");
	} while (std::next_permutation(order, order + 4));
	conflict[0].Gamepad.sThumbLX = 32767;
	conflict[1].Gamepad.sThumbLX = -32768;
	check(merge_gamepads(conflict, 3).sThumbLX == 0, "signed full-scale endpoints have equal strength");
	conflict[1] = conflict[0];
	check(merge_gamepads(conflict, 3).sThumbLX == 32767, "identical inputs never double");
	conflict[0].Gamepad.sThumbLX = 100;
	conflict[1].Gamepad.sThumbLX = 100;
	check(merge_gamepads(conflict, 3).sThumbLX == 100, "small analog values preserved");
	conflict[0].dwPacketNumber = 123;
	conflict[1].dwPacketNumber = 456;
	check(merge_gamepads(conflict, 3).sThumbLX == 100, "packet counters do not affect merge");
	check(merge_gamepads(conflict, 0).sThumbLX == 0, "stale state excluded by connection mask");
	padfix_state_t shared = {};
	publish_state(&shared, pads);
	check(shared.connected_slots == 8 && shared.connected && shared.thumb_ly == -32768, "publisher sends merged values and connection mask");
	check(shared.seq == 2 && shared.heartbeat == 1, "publisher heartbeat and consistent sequence");
	const char *newlines[] = {"\n", "\r\n"};
	for (const char *nl : newlines) {
		steering_case("xinput.xinput_gamepad_1", "joy2.x", 1, "xinput.xinput_gamepad_2", "joy2.x", nl);
		steering_case("di8.custom_wheel", "joy2.x", 1, "di8.custom_wheel", "joy2.x", nl);
		merged_steering_case("xinput.xinput_gamepad_2", "joy2.x", "joy3.lx", nl);
		merged_steering_case("", "joy2.x", "joy3.lx", nl);
		merged_steering_case("xinput.xinput_gamepad_1", "joy3.lx", "joy3.lx", nl);
		merged_steering_case("di8.custom_wheel", "joy2.x", "joy2.x", nl);
		merged_steering_case("xinput.xinput_gamepad_1", "keyboard.a", "keyboard.a", nl);
		merged_steering_case("xinput.xinput_gamepad_1", "joy2.x", "joy2.x", nl, false);
		const std::string mix = "memory(j_steer_c?1, j_steer?0) - j_steer?0";
		const std::string merged = "memory(j_steer_c?1, joy.lx?0) - joy.lx?0";
		rumble_case("xinput.xinput_gamepad_2", "joy.lx", mix, "joy2.x", merged, 1, FIX_CHANGED, nl);
		rumble_case("xinput.xinput_gamepad_2", "joy2.x", mix, "joy2.x", merged, 1, FIX_CHANGED, nl);
		rumble_case("xinput.xinput_gamepad_2", "joy2.x", merged, "joy2.x", merged, 1, FIX_NOTHING_TO_DO, nl);
		rumble_case("xinput.xinput_gamepad_1", "joy.lx", mix, "joy4.x", merged, 1, FIX_CHANGED, nl);
		rumble_case("xinput.xinput_gamepad_1", "joy.lx", mix, "joy.lx", mix, 1, FIX_ERROR, nl, false);
		rumble_case("di8.wheel", "joy2.x", mix, "joy2.x", mix, 1, FIX_NOTHING_TO_DO, nl);
		rumble_case("xinput.xinput_gamepad_2", "keyboard.a", mix, "keyboard.a", mix, 1, FIX_NOTHING_TO_DO, nl);
		rumble_case("xinput.xinput_gamepad_2", "joy.lx", "custom.axis?0", "joy.lx", "custom.axis?0", 1, FIX_ERROR, nl);
		rumble_case("xinput.xinput_gamepad_2", "joy2.x", mix, "joy2.x", mix, 1, FIX_NOTHING_TO_DO, nl, true, false);
		rumble_case("xinput.xinput_gamepad_2", "joy.lx", mix, "joy.lx", mix, 4, FIX_ERROR, nl);
	}
	std::string tokens = "j_steer_c?1 + other.j_steer?0 + j_steer2?0 - j_steer?0 + j_steer?0";
	check(replace_input_reference(tokens, "j_steer", "joy.lx") == 2, "replace complete input identifiers only");
	check(tokens == "j_steer_c?1 + other.j_steer?0 + j_steer2?0 - joy.lx?0 + joy.lx?0", "centering flags and unrelated identifiers preserved");
	if (argc > 1) sdk_integration(argv[1]);
	printf("PASS: %u checks (simultaneous merge, hotplug, conflicts, profile repair%s)\n", checks, argc > 1 ? ", IPC and SDK" : "");
	return 0;
}
