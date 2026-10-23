/**
 * @file host_test.cpp
 * @brief Loads padfix.dll the way the game does, registers its device and pulls
 * events for a while, printing what a game would receive.
 *
 * Usage: host_test.exe <path to padfix.dll> [seconds]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scssdk_input.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_input_eut2.h"

typedef SCSAPI_RESULT_FPTR(init_fn_t)(const scs_u32_t, const scs_input_init_params_t *const);
typedef SCSAPI_VOID_FPTR(shutdown_fn_t)(void);

scs_input_device_t registered;
scs_input_device_input_t inputs[SCS_INPUT_MAX_INPUT_COUNT];

SCSAPI_VOID test_log(const scs_log_type_t type, const scs_string_t message)
{
	printf("  game log [%u]: %s\n", type, message);
}

SCSAPI_RESULT test_register(const scs_input_device_t *const device)
{
	registered = *device;
	memcpy(inputs, device->inputs, device->input_count * sizeof(inputs[0]));
	registered.inputs = inputs;
	return SCS_RESULT_ok;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return 2;
	}
	const double seconds = (argc > 2) ? atof(argv[2]) : 1.5;

	HMODULE dll = LoadLibraryA(argv[1]);
	if (! dll) {
		printf("LoadLibrary failed: %lu\n", GetLastError());
		return 1;
	}
	init_fn_t init = reinterpret_cast<init_fn_t>(GetProcAddress(dll, "scs_input_init"));
	shutdown_fn_t shutdown = reinterpret_cast<shutdown_fn_t>(GetProcAddress(dll, "scs_input_shutdown"));
	if (! init || ! shutdown) {
		printf("exports missing\n");
		return 1;
	}

	scs_input_init_params_v100_t params;
	memset(&params, 0, sizeof(params));
	params.common.game_name = "Euro Truck Simulator 2";
	params.common.game_id = SCS_GAME_ID_EUT2;
	params.common.game_version = SCS_INPUT_EUT2_GAME_VERSION_CURRENT;
	params.common.log = test_log;
	params.register_device = test_register;

	printf("unsupported version check: %d (expect %d)\n", init(SCS_MAKE_VERSION(2, 0), &params), SCS_RESULT_unsupported);
	const scs_result_t result = init(SCS_INPUT_VERSION_1_00, &params);
	printf("init: %d\n", result);
	if (result != SCS_RESULT_ok) {
		return 1;
	}
	printf("device '%s' (%s), type %u, %u inputs\n", registered.name, registered.display_name, registered.type, registered.input_count);

	// Pull events like the game: once per "frame" until not_found.

	const DWORD end = GetTickCount() + static_cast<DWORD>(seconds * 1000);
	unsigned frames = 0;
	unsigned last_count = 0;
	bool bad_index = false;
	while (GetTickCount() < end) {
		scs_u32_t flags = SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame | (frames == 0 ? SCS_INPUT_EVENT_CALLBACK_FLAG_first_after_activation : 0);
		unsigned count = 0;
		scs_input_event_t ev;
		while (registered.input_event_callback(&ev, flags, registered.callback_context) == SCS_RESULT_ok) {
			if (ev.input_index >= registered.input_count) {
				bad_index = true;
			}
			flags = 0;
			++count;
			if (count > registered.input_count + 5) {
				break;
			}
		}
		last_count = count;
		++frames;
		Sleep(16);
	}
	printf("frames: %u, events in last frame: %u, out-of-range index: %s\n", frames, last_count, bad_index ? "YES" : "no");

	shutdown();
	FreeLibrary(dll);
	return 0;
}
