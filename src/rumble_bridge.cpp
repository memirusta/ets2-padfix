#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <string.h>
#include "rumble_bridge.h"
#include "rumble_shared.h"
#include "../third_party/minhook/include/MinHook.h"

namespace {
typedef DWORD (WINAPI *get_state_t)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *get_caps_t)(DWORD, DWORD, XINPUT_CAPABILITIES *);
typedef DWORD (WINAPI *set_state_t)(DWORD, XINPUT_VIBRATION *);
typedef void (WINAPI *enable_t)(BOOL);
get_state_t original_state = nullptr;
get_caps_t original_caps = nullptr;
enable_t original_enable = nullptr;
LPVOID targets[4] = {};
HMODULE xinput = nullptr;
HANDLE mapping = nullptr;
rumble_shared_t *shared = nullptr;
const padfix_state_t *input_state = nullptr;
SRWLOCK writer_lock = SRWLOCK_INIT;
SRWLOCK cache_lock = SRWLOCK_INIT;
XINPUT_STATE cached_input = {};
volatile LONG active = 0, input_ready = 0, callbacks = 0, navigation_enabled = 1, driving = 0;
bool owns_minhook = false;
const char *error_text = "not started";

struct call_guard_t {
	call_guard_t() { InterlockedIncrement(&callbacks); }
	~call_guard_t() { InterlockedDecrement(&callbacks); }
};
void publish(const XINPUT_VIBRATION *value, bool force_disable = false)
{
	AcquireSRWLockExclusive(&writer_lock);
	if (shared) {
		auto &c = shared->command;
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&c.seq));
		c.alive_ms = GetTickCount();
		c.enabled = !force_disable && navigation_enabled && driving ? 1 : 0;
		if (value) {
			c.left_motor = c.enabled ? value->wLeftMotorSpeed : 0;
			c.right_motor = c.enabled ? value->wRightMotorSpeed : 0;
			++c.command_id;
			if (!c.command_id) ++c.command_id;
		}
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&c.seq));
	}
	ReleaseSRWLockExclusive(&writer_lock);
}
DWORD WINAPI get_state(DWORD index, XINPUT_STATE *out)
{
	call_guard_t guard;
	if (index != 0) return original_state(index, out);
	if (!out) return ERROR_BAD_ARGUMENTS;
	*out = {};
	if (active && input_ready && navigation_enabled && input_state) {
		padfix_state_t snapshot = {};
		bool consistent = false;
		for (unsigned attempt = 0; attempt < 16; ++attempt) {
			const uint32_t before = input_state->seq;
			MemoryBarrier();
			if (before & 1) continue;
			memcpy(&snapshot, const_cast<const padfix_state_t *>(input_state), sizeof(snapshot));
			MemoryBarrier();
			if (input_state->seq != before) continue;
			if (snapshot.magic == PADFIX_MAGIC && snapshot.connected) {
				out->dwPacketNumber = snapshot.heartbeat;
				out->Gamepad = {snapshot.buttons, snapshot.left_trigger, snapshot.right_trigger,
					snapshot.thumb_lx, snapshot.thumb_ly, snapshot.thumb_rx, snapshot.thumb_ry};
			}
			consistent = true;
			break;
		}
		if (consistent) {
			AcquireSRWLockExclusive(&cache_lock);
			cached_input = *out;
			ReleaseSRWLockExclusive(&cache_lock);
		}
		else {
			AcquireSRWLockShared(&cache_lock);
			*out = cached_input;
			ReleaseSRWLockShared(&cache_lock);
		}
	}
	// Remain present, with neutral input, when every physical pad disconnects.
	return ERROR_SUCCESS;
}
DWORD WINAPI get_caps(DWORD index, DWORD flags, XINPUT_CAPABILITIES *out)
{
	call_guard_t guard;
	if (index != 0) return original_caps(index, flags, out);
	if (!out || (flags != 0 && flags != XINPUT_FLAG_GAMEPAD)) return ERROR_BAD_ARGUMENTS;
	*out = {};
	out->Type = XINPUT_DEVTYPE_GAMEPAD;
	out->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
	out->Flags = XINPUT_CAPS_FFB_SUPPORTED;
	out->Gamepad = {0xf3ff, 255, 255, 32767, 32767, 32767, 32767};
	out->Vibration = {65535, 65535};
	return ERROR_SUCCESS;
}
DWORD WINAPI set_state(DWORD index, XINPUT_VIBRATION *value)
{
	call_guard_t guard;
	if (index >= XUSER_MAX_COUNT || !value) return ERROR_BAD_ARGUMENTS;
	// Only the stable facade owns output; ignore cleanup zeros for other slots.
	if (index == 0 && active) publish(value);
	return ERROR_SUCCESS;
}
void WINAPI enable(BOOL value)
{
	call_guard_t guard;
	const XINPUT_VIBRATION zero = {};
	InterlockedExchange(&navigation_enabled, value ? 1 : 0);
	publish(&zero);
	if (!value) InterlockedExchange(&input_ready, 0);
#ifndef PADFIX_RUMBLE_TEST
	original_enable(value);
#endif
}
}

bool rumble_bridge_start(const padfix_state_t *input)
{
	if (active) return true;
	if (!input) { error_text = "missing input mapping"; return false; }
	mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(rumble_shared_t), PADFIX_RUMBLE_SHM_NAME);
	if (!mapping) { error_text = "rumble mapping creation failed"; return false; }
	shared = static_cast<rumble_shared_t *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rumble_shared_t)));
	if (!shared) { error_text = "rumble mapping view failed"; rumble_bridge_stop(); return false; }
	if (shared->command.magic == PADFIX_RUMBLE_MAGIC && shared->command.game_pid != GetCurrentProcessId()) {
		HANDLE owner = OpenProcess(SYNCHRONIZE, FALSE, shared->command.game_pid);
		const bool live = owner ? WaitForSingleObject(owner, 0) != WAIT_OBJECT_0 : GetLastError() != ERROR_INVALID_PARAMETER;
		if (owner) CloseHandle(owner);
		if (live) { error_text = "rumble channel owned by another game session"; rumble_bridge_stop(); return false; }
	}
	shared->command = {};
	shared->command.magic = PADFIX_RUMBLE_MAGIC;
	shared->command.game_pid = GetCurrentProcessId();
	shared->command.enabled = driving ? 1 : 0;
	shared->command.alive_ms = GetTickCount();
	input_state = input;
	cached_input = {};
	InterlockedExchange(&navigation_enabled, 1);
	xinput = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!xinput) { error_text = "system XInput 1.4 unavailable"; rumble_bridge_stop(); return false; }
	MH_STATUS status = MH_Initialize();
	if (status != MH_OK) { error_text = MH_StatusToString(status); rumble_bridge_stop(); return false; }
	owns_minhook = true;
	const char *names[] = {"XInputGetState", "XInputGetCapabilities", "XInputSetState", "XInputEnable"};
	LPVOID detours[] = {reinterpret_cast<LPVOID>(&get_state), reinterpret_cast<LPVOID>(&get_caps), reinterpret_cast<LPVOID>(&set_state), reinterpret_cast<LPVOID>(&enable)};
	LPVOID originals[4] = {};
	for (unsigned i = 0; i < 4; ++i) {
		targets[i] = reinterpret_cast<LPVOID>(GetProcAddress(xinput, names[i]));
		if (!targets[i]) { error_text = "required XInput export missing"; rumble_bridge_stop(); return false; }
		status = MH_CreateHook(targets[i], detours[i], &originals[i]);
		if (status != MH_OK) { error_text = MH_StatusToString(status); rumble_bridge_stop(); return false; }
	}
	original_state = reinterpret_cast<get_state_t>(originals[0]);
	original_caps = reinterpret_cast<get_caps_t>(originals[1]);
	original_enable = reinterpret_cast<enable_t>(originals[3]);
	for (LPVOID target : targets) MH_QueueEnableHook(target);
	status = MH_ApplyQueued();
	if (status != MH_OK) { error_text = MH_StatusToString(status); rumble_bridge_stop(); return false; }
	InterlockedExchange(&active, 1);
	error_text = "none";
	return true;
}
void rumble_bridge_tick(bool alive)
{
	if (!active) return;
	InterlockedExchange(&input_ready, alive ? 1 : 0);
	publish(nullptr);
}
void rumble_bridge_set_driving(bool value)
{
	InterlockedExchange(&driving, value ? 1 : 0);
	const XINPUT_VIBRATION zero = {};
	publish(&zero);
}
void rumble_bridge_stop()
{
	const XINPUT_VIBRATION zero = {};
	if (active) publish(&zero, true);
	InterlockedExchange(&active, 0);
	InterlockedExchange(&input_ready, 0);
	if (owns_minhook) {
		for (LPVOID target : targets) if (target) MH_QueueDisableHook(target);
		MH_ApplyQueued();
		const ULONGLONG until = GetTickCount64() + 1000;
		while (callbacks && GetTickCount64() < until) Sleep(1);
		// Do not free trampolines underneath a delayed callback.
		if (callbacks) { error_text = "XInput callbacks did not drain"; return; }
		for (LPVOID &target : targets) { if (target) MH_RemoveHook(target); target = nullptr; }
		MH_Uninitialize();
		owns_minhook = false;
	}
	input_state = nullptr;
	AcquireSRWLockExclusive(&writer_lock);
	if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
	ReleaseSRWLockExclusive(&writer_lock);
	if (mapping) { CloseHandle(mapping); mapping = nullptr; }
	if (xinput) { FreeLibrary(xinput); xinput = nullptr; }
}
const char *rumble_bridge_error() { return error_text; }
