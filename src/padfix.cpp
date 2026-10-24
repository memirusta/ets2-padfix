/**
 * @file padfix.cpp
 * @brief ETS2/ATS input plugin exposing merged XInput pads as one SDK device whose
 * name never changes ("padfix"), so bindings survive reconnects, DirectInput
 * GUID changes and Steam Input toggling.
 *
 * The pads are read and merged by padfix_helper.exe (started from here) outside the
 * game process; this plugin only turns the shared state into input events.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <xinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "scssdk_input.h"
#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_input_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_input_ats.h"

#include "shared.h"
#include "rumble_bridge.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Inputs of the device. Order defines the input index.

enum axis_t
{
	AXIS_LX, AXIS_LY, AXIS_RX, AXIS_RY, AXIS_LT, AXIS_RT,
	AXIS_COUNT
};

enum button_t
{
	BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_LB, BUTTON_RB,
	BUTTON_BACK, BUTTON_START, BUTTON_LS, BUTTON_RS,
	BUTTON_DPAD_UP, BUTTON_DPAD_DOWN, BUTTON_DPAD_LEFT, BUTTON_DPAD_RIGHT,
	BUTTON_LT_PRESS, BUTTON_RT_PRESS,
	BUTTON_LSTICK_UP, BUTTON_LSTICK_DOWN, BUTTON_LSTICK_LEFT, BUTTON_LSTICK_RIGHT,
	BUTTON_RSTICK_UP, BUTTON_RSTICK_DOWN, BUTTON_RSTICK_LEFT, BUTTON_RSTICK_RIGHT,
	BUTTON_COUNT
};

const scs_input_device_input_t INPUTS[AXIS_COUNT + BUTTON_COUNT] = {
	{"lx", "Left stick X", SCS_VALUE_TYPE_float},
	{"ly", "Left stick Y", SCS_VALUE_TYPE_float},
	{"rx", "Right stick X", SCS_VALUE_TYPE_float},
	{"ry", "Right stick Y", SCS_VALUE_TYPE_float},
	{"lt", "LT", SCS_VALUE_TYPE_float},
	{"rt", "RT", SCS_VALUE_TYPE_float},
	{"a", "A", SCS_VALUE_TYPE_bool},
	{"b", "B", SCS_VALUE_TYPE_bool},
	{"x", "X", SCS_VALUE_TYPE_bool},
	{"y", "Y", SCS_VALUE_TYPE_bool},
	{"lb", "LB", SCS_VALUE_TYPE_bool},
	{"rb", "RB", SCS_VALUE_TYPE_bool},
	{"back", "Back", SCS_VALUE_TYPE_bool},
	{"start", "Start", SCS_VALUE_TYPE_bool},
	{"ls", "Left stick press", SCS_VALUE_TYPE_bool},
	{"rs", "Right stick press", SCS_VALUE_TYPE_bool},
	{"dpad_up", "Dpad up", SCS_VALUE_TYPE_bool},
	{"dpad_down", "Dpad down", SCS_VALUE_TYPE_bool},
	{"dpad_left", "Dpad left", SCS_VALUE_TYPE_bool},
	{"dpad_right", "Dpad right", SCS_VALUE_TYPE_bool},
	{"lt_press", "LT press", SCS_VALUE_TYPE_bool},
	{"rt_press", "RT press", SCS_VALUE_TYPE_bool},
	{"lstick_up", "Left stick up", SCS_VALUE_TYPE_bool},
	{"lstick_down", "Left stick down", SCS_VALUE_TYPE_bool},
	{"lstick_left", "Left stick left", SCS_VALUE_TYPE_bool},
	{"lstick_right", "Left stick right", SCS_VALUE_TYPE_bool},
	{"rstick_up", "Right stick up", SCS_VALUE_TYPE_bool},
	{"rstick_down", "Right stick down", SCS_VALUE_TYPE_bool},
	{"rstick_left", "Right stick left", SCS_VALUE_TYPE_bool},
	{"rstick_right", "Right stick right", SCS_VALUE_TYPE_bool},
};

const WORD BUTTON_MASKS[BUTTON_LT_PRESS] = {
	XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
	XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
	XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
	XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
	XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
};

// Stick deflection needed for the stick direction buttons.
const float STICK_BUTTON_THRESHOLD = 0.5f;

// Helper is considered dead when its heartbeat stops for this long.
const DWORD HELPER_STALE_MS = 1000;
const DWORD HELPER_RESTART_INTERVAL_MS = 3000;

struct device_data_t
{
	float axes[AXIS_COUNT];
	bool buttons[BUTTON_COUNT];
	unsigned next_reported_input;

	padfix_state_t snapshot;
	uint32_t last_heartbeat;
	DWORD last_heartbeat_time;
	DWORD last_restart_time;
	uint32_t last_connected_slots;
	bool pressed_once[BUTTON_COUNT];
};

device_data_t device;

scs_log_t game_log = NULL;
HANDLE mapping = NULL;
const padfix_state_t *shared_state = NULL;

// global_controls.sii of the running game; the helper fixes it after the game exits.
wchar_t global_controls_path[MAX_PATH] = L"";

void log_line(const scs_log_type_t type, const char *const format, ...)
{
	if (! game_log) {
		return;
	}
	char text[256];
	strcpy_s(text, "[padfix] ");
	va_list args;
	va_start(args, format);
	vsnprintf(text + strlen(text), sizeof(text) - strlen(text), format, args);
	va_end(args);
	game_log(type, text);
}

void find_global_controls(const char *const game_id)
{
	global_controls_path[0] = 0;
	const wchar_t *folder = NULL;
	if (strcmp(game_id, SCS_GAME_ID_EUT2) == 0) {
		folder = L"Euro Truck Simulator 2";
	}
	else if (strcmp(game_id, SCS_GAME_ID_ATS) == 0) {
		folder = L"American Truck Simulator";
	}
	if (! folder) {
		return;
	}
	PWSTR documents = NULL;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &documents))) {
		swprintf_s(global_controls_path, L"%ls\\%ls\\global_controls.sii", documents, folder);
	}
	CoTaskMemFree(documents);
}

bool is_helper_running(void)
{
	HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, PADFIX_MUTEX_NAME);
	if (! mutex) {
		return false;
	}
	CloseHandle(mutex);
	return true;
}

void start_helper(void)
{
	device.last_restart_time = GetTickCount();
	if (is_helper_running()) {
		return;
	}

	// The helper lives in plugins\padfix\ next to this DLL.

	HMODULE module = NULL;
	if (! GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&start_helper), &module)) {
		log_line(SCS_LOG_TYPE_error, "Unable to locate the plugin module");
		return;
	}
	wchar_t path[MAX_PATH];
	const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		log_line(SCS_LOG_TYPE_error, "Unable to get the plugin path");
		return;
	}
	wchar_t *const slash = wcsrchr(path, L'\\');
	if (! slash) {
		return;
	}
	slash[1] = 0;
	if (wcscat_s(path, L"padfix\\padfix_helper.exe") != 0) {
		return;
	}

	wchar_t command_line[2 * MAX_PATH + 32];
	swprintf_s(command_line, L"\"%ls\" %lu \"%ls\"", path, GetCurrentProcessId(), global_controls_path);

	STARTUPINFOW startup;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process;
	if (! CreateProcessW(path, command_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
		log_line(SCS_LOG_TYPE_error, "Unable to start padfix_helper.exe (error %lu)", GetLastError());
		return;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	log_line(SCS_LOG_TYPE_message, "Started padfix_helper.exe");
}

bool read_snapshot(padfix_state_t &out)
{
	for (unsigned attempt = 0; attempt < 16; ++attempt) {
		const uint32_t before = shared_state->seq;
		MemoryBarrier();
		if (before & 1) {
			YieldProcessor();
			continue;
		}
		memcpy(&out, const_cast<const padfix_state_t *>(shared_state), sizeof(out));
		MemoryBarrier();
		if (shared_state->seq == before) {
			return true;
		}
	}
	return false;
}

float normalize_stick(const int16_t value)
{
	const float result = static_cast<float>(value) / 32767.0f;
	return (result < -1.0f) ? -1.0f : result;
}

void update_device(device_data_t &data)
{
	read_snapshot(data.snapshot);

	// Treat the pad as disconnected while the helper is not running.

	const DWORD now = GetTickCount();
	if (data.snapshot.heartbeat != data.last_heartbeat) {
		data.last_heartbeat = data.snapshot.heartbeat;
		data.last_heartbeat_time = now;
	}
	const bool helper_alive = (data.snapshot.magic == PADFIX_MAGIC) && (now - data.last_heartbeat_time < HELPER_STALE_MS);
	if (! helper_alive && (now - data.last_restart_time >= HELPER_RESTART_INTERVAL_MS)) {
		start_helper();
	}

	rumble_bridge_tick(helper_alive);
	const bool connected = helper_alive && data.snapshot.connected;
	const uint32_t slots = connected ? data.snapshot.connected_slots : 0;
	if (slots != data.last_connected_slots) {
		data.last_connected_slots = slots;
		if (connected) {
			log_line(SCS_LOG_TYPE_message, "Merging XInput slots (mask 0x%X; bits 0-3 = slots 1-4)", slots);
		}
		else {
			log_line(SCS_LOG_TYPE_message, "All pads disconnected");
		}
	}

	if (! connected) {
		memset(data.axes, 0, sizeof(data.axes));
		memset(data.buttons, 0, sizeof(data.buttons));
		return;
	}

	const padfix_state_t &s = data.snapshot;
	// XInput reports stick up as positive, the game's own gamepad devices as
	// negative; flip Y so look up/down behaves like with those devices.
	const float stick_ly = normalize_stick(s.thumb_ly);
	const float stick_ry = normalize_stick(s.thumb_ry);
	data.axes[AXIS_LX] = normalize_stick(s.thumb_lx);
	data.axes[AXIS_LY] = -stick_ly;
	data.axes[AXIS_RX] = normalize_stick(s.thumb_rx);
	data.axes[AXIS_RY] = -stick_ry;
	data.axes[AXIS_LT] = static_cast<float>(s.left_trigger) / 255.0f;
	data.axes[AXIS_RT] = static_cast<float>(s.right_trigger) / 255.0f;

	for (unsigned i = 0; i < BUTTON_LT_PRESS; ++i) {
		data.buttons[i] = (s.buttons & BUTTON_MASKS[i]) != 0;
	}
	data.buttons[BUTTON_LT_PRESS] = s.left_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	data.buttons[BUTTON_RT_PRESS] = s.right_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	data.buttons[BUTTON_LSTICK_UP] = stick_ly > STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_LSTICK_DOWN] = stick_ly < -STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_LSTICK_LEFT] = data.axes[AXIS_LX] < -STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_LSTICK_RIGHT] = data.axes[AXIS_LX] > STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_RSTICK_UP] = stick_ry > STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_RSTICK_DOWN] = stick_ry < -STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_RSTICK_LEFT] = data.axes[AXIS_RX] < -STICK_BUTTON_THRESHOLD;
	data.buttons[BUTTON_RSTICK_RIGHT] = data.axes[AXIS_RX] > STICK_BUTTON_THRESHOLD;

	// Log the first press of every button so game.log.txt shows what arrives.

	for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
		if (data.buttons[i] && ! data.pressed_once[i]) {
			data.pressed_once[i] = true;
			log_line(SCS_LOG_TYPE_message, "First press: %s", INPUTS[AXIS_COUNT + i].display_name);
		}
	}
}

SCSAPI_RESULT input_event_callback(scs_input_event_t *const event_info, const scs_u32_t flags, const scs_context_t context)
{
	device_data_t &data = *static_cast<device_data_t *>(context);

	if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
		update_device(data);
		data.next_reported_input = 0;
	}

	// The device is small, so every input is reported every frame.

	if (data.next_reported_input >= AXIS_COUNT + BUTTON_COUNT) {
		return SCS_RESULT_not_found;
	}

	const unsigned index = data.next_reported_input++;
	event_info->input_index = index;
	if (index < AXIS_COUNT) {
		event_info->value_float.value = data.axes[index];
	}
	else {
		event_info->value_bool.value = data.buttons[index - AXIS_COUNT] ? 1 : 0;
	}
	return SCS_RESULT_ok;
}

void close_shared_state(void)
{
	if (shared_state) {
		UnmapViewOfFile(shared_state);
		shared_state = NULL;
	}
	if (mapping) {
		CloseHandle(mapping);
		mapping = NULL;
	}
}

SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t *const params)
{
	if (version != SCS_INPUT_VERSION_1_00) {
		return SCS_RESULT_unsupported;
	}

	const scs_input_init_params_v100_t *const version_params = static_cast<const scs_input_init_params_v100_t *>(params);
	game_log = version_params->common.log;

	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0) {
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(SCS_INPUT_EUT2_GAME_VERSION_CURRENT)) {
			log_line(SCS_LOG_TYPE_warning, "Too new major version of the game, some features might behave incorrectly");
		}
	}
	else if (strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(SCS_INPUT_ATS_GAME_VERSION_CURRENT)) {
			log_line(SCS_LOG_TYPE_warning, "Too new major version of the game, some features might behave incorrectly");
		}
	}

	// The helper creates the same mapping; whoever comes first creates it.

	mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(padfix_state_t), PADFIX_SHM_NAME);
	if (! mapping) {
		log_line(SCS_LOG_TYPE_error, "Unable to create shared memory (error %lu)", GetLastError());
		return SCS_RESULT_generic_error;
	}
	shared_state = static_cast<const padfix_state_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(padfix_state_t)));
	if (! shared_state) {
		log_line(SCS_LOG_TYPE_error, "Unable to map shared memory (error %lu)", GetLastError());
		close_shared_state();
		return SCS_RESULT_generic_error;
	}

	memset(&device, 0, sizeof(device));
	device.last_heartbeat_time = GetTickCount();
	find_global_controls(version_params->common.game_id);
	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0 || strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {
		if (rumble_bridge_start(shared_state)) log_line(SCS_LOG_TYPE_message, "Rumble broadcast active; stable XInput slot 1");
		else log_line(SCS_LOG_TYPE_warning, "Rumble broadcast unavailable: %s", rumble_bridge_error());
	}
	start_helper();

	scs_input_device_t device_info;
	memset(&device_info, 0, sizeof(device_info));
	device_info.name = "padfix";
	device_info.display_name = "Stable Gamepad";
	device_info.type = SCS_INPUT_DEVICE_TYPE_generic;
	device_info.input_count = AXIS_COUNT + BUTTON_COUNT;
	device_info.inputs = INPUTS;
	device_info.input_event_callback = input_event_callback;
	device_info.callback_context = &device;

	if (version_params->register_device(&device_info) != SCS_RESULT_ok) {
		log_line(SCS_LOG_TYPE_error, "Unable to register device");
		rumble_bridge_stop();
		close_shared_state();
		return SCS_RESULT_generic_error;
	}

	log_line(SCS_LOG_TYPE_message, "Registered device 'padfix'");
	return SCS_RESULT_ok;
}

SCSAPI_VOID scs_input_shutdown(void)
{
	// The helper keeps running until the game exits so a reinit can reuse it.

	rumble_bridge_stop();
	close_shared_state();
	game_log = NULL;
}

// Only driving/paused state is observed; game-native rumble effects are relayed
// unchanged, rather than synthesizing effects from truck telemetry.
scs_telemetry_unregister_from_event_t rumble_unregister_event = NULL;
SCSAPI_VOID rumble_driving_event(const scs_event_t event, const void *, const scs_context_t)
{
	rumble_bridge_set_driving(event == SCS_TELEMETRY_EVENT_started);
}
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params)
{
	if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
	const auto *p = static_cast<const scs_telemetry_init_params_v100_t *>(params);
	rumble_bridge_set_driving(false);
	if (p->register_for_event(SCS_TELEMETRY_EVENT_started, rumble_driving_event, NULL) != SCS_RESULT_ok) return SCS_RESULT_generic_error;
	if (p->register_for_event(SCS_TELEMETRY_EVENT_paused, rumble_driving_event, NULL) != SCS_RESULT_ok) {
		p->unregister_from_event(SCS_TELEMETRY_EVENT_started);
		return SCS_RESULT_generic_error;
	}
	rumble_unregister_event = p->unregister_from_event;
	return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown(void)
{
	rumble_bridge_set_driving(false);
	if (rumble_unregister_event) {
		rumble_unregister_event(SCS_TELEMETRY_EVENT_started);
		rumble_unregister_event(SCS_TELEMETRY_EVENT_paused);
		rumble_unregister_event = NULL;
	}
}
