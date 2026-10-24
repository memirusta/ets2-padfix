/**
 * @file helper.cpp
 * @brief padfix_helper.exe - merges connected XInput pads and publishes
 * its state in shared memory for padfix.dll.
 *
 * Usage:
 *   padfix_helper.exe <game pid> [global_controls.sii path]
 *     Serves the game until it exits, then makes sure the padfix device is not
 *     hidden in the controls menu (see fix_whitelist). After a broadcast session,
 *     supported padfix profiles retain stable rumble targeting and merged steering.
 *   padfix_helper.exe --fix-whitelist <global_controls.sii path>
 *   padfix_helper.exe --fix-steering <controls.sii path> [XInput slot 1-4]
 *   padfix_helper.exe --fix-merged-steering <controls.sii path>
 *   padfix_helper.exe --fix-merged-rumble <controls.sii path> <XInput slot 1-4>
 *   padfix_helper.exe --fix-broadcast-rumble <controls.sii path>
 *     Opt-in profile fixes only. Exit code 0 = unchanged, 2 = fixed, 1 = error.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <xinput.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <ctype.h>

#include <string>
#include <vector>

#include "shared.h"
#include "controller_merge.h"
#include "controller_sources.h"
#include "rumble_output.h"

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

const DWORD POLL_INTERVAL_MS = 2;

const int FIX_NOTHING_TO_DO = 0;
const int FIX_ERROR = 1;
const int FIX_CHANGED = 2;

// Entry the game writes for our device in global_controls.sii.
const char DEVICE_ENTRY[] = "\"sdk.padfix\"";

// Device names as they appear in controls.sii.
const std::string PADFIX_DEVICE = "sdk.padfix";
bool is_xinput_device(const std::string &name)
{
	const std::string prefix = "xinput.xinput_gamepad_";
	return name.size() == prefix.size() + 1 && name.compare(0, prefix.size(), prefix) == 0
		&& name.back() >= '1' && name.back() <= '4';
}

bool read_file(const wchar_t *const path, std::string &out)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	LARGE_INTEGER size;
	bool ok = GetFileSizeEx(file, &size) && size.QuadPart < 16 * 1024 * 1024;
	if (ok) {
		out.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		ok = out.empty() || (ReadFile(file, &out[0], static_cast<DWORD>(out.size()), &read, NULL) && read == out.size());
	}
	CloseHandle(file);
	return ok;
}

bool write_file_replace(const wchar_t *const path, const std::string &data)
{
	const std::wstring temp = std::wstring(path) + L".padfix.tmp";
	HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, NULL) && written == data.size();
	CloseHandle(file);
	if (! ok || ! MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temp.c_str());
		return false;
	}
	return true;
}

std::string detect_newline(const std::string &data)
{
	return (data.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
}

std::vector<std::string> split_lines(const std::string &data, const std::string &newline)
{
	std::vector<std::string> lines;
	size_t start = 0;
	for (;;) {
		const size_t end = data.find(newline, start);
		if (end == std::string::npos) {
			lines.push_back(data.substr(start));
			return lines;
		}
		lines.push_back(data.substr(start, end - start));
		start = end + newline.size();
	}
}

std::string join_lines(const std::vector<std::string> &lines, const std::string &newline)
{
	std::string result;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i) {
			result += newline;
		}
		result += lines[i];
	}
	return result;
}

std::string trim_left(const std::string &line)
{
	const size_t start = line.find_first_not_of(" \t");
	return (start == std::string::npos) ? std::string() : line.substr(start);
}

/**
 * @brief One SII array attribute: "name: N" followed by "name[i]: value" lines.
 */
struct sii_array_t
{
	size_t count_line;
	std::string indent;
	std::vector<size_t> entry_lines;
	std::vector<std::string> values;

	sii_array_t() : count_line(std::string::npos) {}
};

void find_array(const std::vector<std::string> &lines, const std::string &name, sii_array_t &array)
{
	for (size_t i = 0; i < lines.size(); ++i) {
		const std::string text = trim_left(lines[i]);
		if (text.compare(0, name.size() + 1, name + ":") == 0) {
			array.count_line = i;
			array.indent = lines[i].substr(0, lines[i].size() - text.size());
		}
		else if (text.compare(0, name.size() + 1, name + "[") == 0) {
			const size_t colon = text.find("]: ");
			if (colon != std::string::npos) {
				array.entry_lines.push_back(i);
				array.values.push_back(text.substr(colon + 3));
			}
		}
	}
}

void emit_array(std::vector<std::string> &out, const std::string &name, const sii_array_t &array)
{
	out.push_back(array.indent + name + ": " + std::to_string(array.values.size()));
	for (size_t i = 0; i < array.values.size(); ++i) {
		out.push_back(array.indent + name + "[" + std::to_string(i) + "]: " + array.values[i]);
	}
}

/**
 * @brief Moves the padfix device from ui_blacklisted to ui_whitelisted.
 *
 * The game sometimes blacklists the device because it sees the same pad through
 * its own XInput/DirectInput devices too, which hides it in the controls menu.
 * Must run while the game is not running, as the game rewrites the file.
 */
int fix_whitelist(const wchar_t *const path)
{
	std::string data;
	if (! read_file(path, data)) {
		return FIX_ERROR;
	}
	if (data.compare(0, 8, "SiiNunit") != 0) {
		return FIX_ERROR; // binary or encrypted format, leave it alone
	}

	const std::string newline = detect_newline(data);
	const std::vector<std::string> lines = split_lines(data, newline);

	sii_array_t white;
	sii_array_t black;
	find_array(lines, "ui_whitelisted", white);
	find_array(lines, "ui_blacklisted", black);
	if (white.count_line == std::string::npos || black.count_line == std::string::npos) {
		return FIX_ERROR;
	}

	bool blacklisted = false;
	for (size_t i = 0; i < black.values.size();) {
		if (black.values[i] == DEVICE_ENTRY) {
			black.values.erase(black.values.begin() + i);
			blacklisted = true;
		}
		else {
			++i;
		}
	}
	if (! blacklisted) {
		return FIX_NOTHING_TO_DO;
	}
	bool whitelisted = false;
	for (size_t i = 0; i < white.values.size(); ++i) {
		whitelisted = whitelisted || (white.values[i] == DEVICE_ENTRY);
	}
	if (! whitelisted) {
		white.values.push_back(DEVICE_ENTRY);
	}

	// Rebuild the file, replacing both arrays where their count lines were.

	std::vector<bool> skip(lines.size(), false);
	for (size_t i = 0; i < white.entry_lines.size(); ++i) {
		skip[white.entry_lines[i]] = true;
	}
	for (size_t i = 0; i < black.entry_lines.size(); ++i) {
		skip[black.entry_lines[i]] = true;
	}
	std::vector<std::string> out;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i == white.count_line) {
			emit_array(out, "ui_whitelisted", white);
		}
		else if (i == black.count_line) {
			emit_array(out, "ui_blacklisted", black);
		}
		else if (! skip[i]) {
			out.push_back(lines[i]);
		}
	}
	CopyFileW(path, (std::wstring(path) + L".padfix.bak").c_str(), FALSE);
	return write_file_replace(path, join_lines(out, newline)) ? FIX_CHANGED : FIX_ERROR;
}

/**
 * @brief A "device <alias> `<device>`" line of controls.sii.
 */
struct device_line_t
{
	size_t line;
	std::string alias;
	std::string device;
	size_t value_start; // position of the device name inside the line
	size_t value_end;
};

bool parse_device_line(const std::string &text, const size_t index, device_line_t &out)
{
	const size_t key = text.find("\"device ");
	if (key == std::string::npos) {
		return false;
	}
	const size_t name_start = key + 8;
	const size_t name_end = text.find(' ', name_start);
	const size_t open = (name_end == std::string::npos) ? std::string::npos : text.find('`', name_end);
	const size_t close = (open == std::string::npos) ? std::string::npos : text.find('`', open + 1);
	if (close == std::string::npos) {
		return false;
	}
	out.line = index;
	out.alias = text.substr(name_start, name_end - name_start);
	out.device = text.substr(open + 1, close - open - 1);
	out.value_start = open + 1;
	out.value_end = close;
	return true;
}

/**
 * @brief Moves the steering axis from the padfix device to the game's XInput device.
 *
 * The game vibrates the pad only through its own XInput device, so steering
 * bound to padfix means no vibration. Only "input j_steer `<padfix>.lx`" is
 * changed (to the XInput device's "x", same direction); every other binding
 * stays on padfix. Also repairs steering that points at an emptied or previous XInput slot.
 * Explicit native-steering workaround only; merged input is bypassed on this axis.
 * Must run while the game is not running, as the game rewrites the file.
 */
int fix_steering(const wchar_t *const path, const DWORD slot = 0)
{
	if (slot >= XUSER_MAX_COUNT) return FIX_NOTHING_TO_DO;
	const std::string xinput_device = "xinput.xinput_gamepad_" + std::to_string(slot + 1);
	std::string data;
	if (! read_file(path, data)) {
		return FIX_ERROR;
	}
	if (data.compare(0, 8, "SiiNunit") != 0) {
		return FIX_ERROR; // binary or encrypted format, leave it alone
	}
	const std::string newline = detect_newline(data);
	std::vector<std::string> lines = split_lines(data, newline);

	std::vector<device_line_t> devices;
	size_t steer_line = std::string::npos;
	size_t steer_start = 0;
	size_t steer_end = 0;
	for (size_t i = 0; i < lines.size(); ++i) {
		device_line_t device;
		if (parse_device_line(lines[i], i, device)) {
			devices.push_back(device);
			continue;
		}
		const size_t key = lines[i].find("\"input j_steer `");
		if (key != std::string::npos) {
			steer_start = key + 16;
			steer_end = lines[i].find('`', steer_start);
			if (steer_end != std::string::npos) {
				steer_line = i;
			}
		}
	}
	if (steer_line == std::string::npos) {
		return FIX_NOTHING_TO_DO;
	}

	const device_line_t *padfix = NULL;
	const device_line_t *xinput = NULL;
	const device_line_t *free_slot = NULL;
	const device_line_t *old_xinput = NULL;
	for (size_t i = 0; i < devices.size(); ++i) {
		const device_line_t &d = devices[i];
		if (d.device == PADFIX_DEVICE && ! padfix) {
			padfix = &d;
		}
		else if (d.device == xinput_device && ! xinput) {
			xinput = &d;
		}
		else if (is_xinput_device(d.device) && ! old_xinput) {
			old_xinput = &d;
		}
		else if (d.device.empty() && d.alias.compare(0, 3, "joy") == 0 && ! free_slot) {
			free_slot = &d;
		}
	}
	if (! padfix) {
		return FIX_NOTHING_TO_DO;
	}

	const std::string steer = lines[steer_line].substr(steer_start, steer_end - steer_start);
	const device_line_t *target = NULL;
	if (steer == padfix->alias + ".lx") {
		target = xinput ? xinput : (old_xinput ? old_xinput : free_slot);
	}
	else {
		// Repair an emptied slot or a native XInput binding left on the old connection slot.
		for (size_t i = 0; i < devices.size() && ! target; ++i) {
			if ((devices[i].device.empty() || is_xinput_device(devices[i].device)) && steer == devices[i].alias + ".x") {
				target = xinput ? xinput : &devices[i];
			}
		}
		if (! target) {
			return FIX_NOTHING_TO_DO;
		}
	}
	if (! target) {
		return FIX_NOTHING_TO_DO; // no free slot for the XInput device
	}

	if (target->device == xinput_device && steer == target->alias + ".x") {
		return FIX_NOTHING_TO_DO;
	}
	if (target->device != xinput_device) {
		std::string &text = lines[target->line];
		text = text.substr(0, target->value_start) + xinput_device + text.substr(target->value_end);
	}
	lines[steer_line] = lines[steer_line].substr(0, steer_start) + target->alias + ".x" + lines[steer_line].substr(steer_end);

	CopyFileW(path, (std::wstring(path) + L".padfix.bak").c_str(), FALSE);
	return write_file_replace(path, join_lines(lines, newline)) ? FIX_CHANGED : FIX_ERROR;
}

/** Moves only native/empty-slot steering to the existing merged padfix axis.
 * Explicitly invoked with the game closed. Wheel and keyboard steering stay put.
 */
int fix_merged_steering(const wchar_t *const path)
{
	std::string data;
	if (!read_file(path, data) || data.compare(0, 8, "SiiNunit") != 0) return FIX_ERROR;
	const std::string newline = detect_newline(data);
	std::vector<std::string> lines = split_lines(data, newline);
	std::vector<device_line_t> devices;
	std::string padfix_alias;
	for (size_t i = 0; i < lines.size(); ++i) {
		device_line_t d;
		if (!parse_device_line(lines[i], i, d)) continue;
		devices.push_back(d);
		if (d.device == PADFIX_DEVICE) padfix_alias = d.alias;
	}
	if (padfix_alias.empty()) return FIX_NOTHING_TO_DO;
	for (size_t i = 0; i < lines.size(); ++i) {
		const size_t key = lines[i].find("\"input j_steer `");
		if (key == std::string::npos) continue;
		const size_t start = key + 16;
		const size_t end = lines[i].find('`', start);
		if (end == std::string::npos) continue;
		const std::string axis = lines[i].substr(start, end - start);
		for (const auto &d : devices) {
			if ((is_xinput_device(d.device) || d.device.empty()) && axis == d.alias + ".x") {
				lines[i].replace(start, end - start, padfix_alias + ".lx");
				if (!CopyFileW(path, (std::wstring(path) + L".padfix.bak").c_str(), FALSE)) return FIX_ERROR;
				return write_file_replace(path, join_lines(lines, newline)) ? FIX_CHANGED : FIX_ERROR;
			}
		}
	}
	return FIX_NOTHING_TO_DO;
}

bool input_name_char(char ch)
{
	return isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
}

size_t replace_input_reference(std::string &expression, const std::string &from, const std::string &to)
{
	size_t count = 0;
	for (size_t pos = 0; (pos = expression.find(from, pos)) != std::string::npos;) {
		const size_t end = pos + from.size();
		if ((pos && input_name_char(expression[pos - 1])) || (end < expression.size() && input_name_char(expression[end]))) {
			pos = end;
			continue;
		}
		expression.replace(pos, from.size(), to);
		pos += to.size();
		++count;
	}
	return count;
}

// Retain a native steering alias for the game's rumble target, but read the
// merged SDK axis in the actual steering mix. No synthesized rumble or input
// hooks are involved. Apply explicitly with the game closed, then validate in-game.
int fix_merged_rumble(const wchar_t *const path, DWORD slot)
{
	if (slot >= XUSER_MAX_COUNT) return FIX_ERROR;
	std::string data;
	if (!read_file(path, data) || data.compare(0, 8, "SiiNunit") != 0) return FIX_ERROR;
	const std::string newline = detect_newline(data);
	std::vector<std::string> lines = split_lines(data, newline);
	std::vector<device_line_t> devices;
	std::string sdk_axis;
	const std::string native_id = "xinput.xinput_gamepad_" + std::to_string(slot + 1);
	const device_line_t *native = NULL;
	const device_line_t *empty = NULL;
	size_t steer_line = std::string::npos, mix_line = std::string::npos;
	size_t steer_start = 0, steer_end = 0, mix_start = 0, mix_end = 0;
	const std::string steer_key = "\"input j_steer `";
	const std::string mix_key = "\"mix steering `";
	for (size_t i = 0; i < lines.size(); ++i) {
		device_line_t d;
		if (parse_device_line(lines[i], i, d)) {
			devices.push_back(d);
			if (d.device == PADFIX_DEVICE) sdk_axis = d.alias + ".lx";
		}
		const size_t steer = lines[i].find(steer_key);
		const size_t mix = lines[i].find(mix_key);
		if (steer != std::string::npos) {
			if (steer_line != std::string::npos) return FIX_ERROR;
			steer_line = i; steer_start = steer + steer_key.size(); steer_end = lines[i].find('`', steer_start);
		}
		if (mix != std::string::npos) {
			if (mix_line != std::string::npos) return FIX_ERROR;
			mix_line = i; mix_start = mix + mix_key.size(); mix_end = lines[i].find('`', mix_start);
		}
	}
	if (sdk_axis.empty()) return FIX_NOTHING_TO_DO;
	if (steer_line == std::string::npos || mix_line == std::string::npos || steer_end == std::string::npos || mix_end == std::string::npos) return FIX_ERROR;
	const std::string old_axis = lines[steer_line].substr(steer_start, steer_end - steer_start);
	bool supported = old_axis == sdk_axis;
	for (const auto &d : devices) {
		if (d.device == native_id) native = &d;
		if (d.device.empty() && d.alias.compare(0, 3, "joy") == 0 && !empty) empty = &d;
		if ((is_xinput_device(d.device) || d.device.empty()) && old_axis == d.alias + ".x") supported = true;
	}
	if (!supported) return FIX_NOTHING_TO_DO; // preserve a wheel, keyboard or custom axis
	if (!native) native = empty;
	if (!native) return FIX_ERROR; // do not hijack another controller's device alias
	std::string expression = lines[mix_line].substr(mix_start, mix_end - mix_start);
	const size_t replaced = replace_input_reference(expression, "j_steer", sdk_axis);
	if (!replaced) {
		std::string probe = expression;
		if (!replace_input_reference(probe, sdk_axis, sdk_axis)) return FIX_ERROR;
	}
	lines[mix_line].replace(mix_start, mix_end - mix_start, expression);
	lines[steer_line].replace(steer_start, steer_end - steer_start, native->alias + ".x");
	if (native->device.empty()) lines[native->line].replace(native->value_start, native->value_end - native->value_start, native_id);
	const std::string result = join_lines(lines, newline);
	if (result == data) return FIX_NOTHING_TO_DO;
	if (!CopyFileW(path, (std::wstring(path) + L".padfix.bak").c_str(), FALSE)) return FIX_ERROR;
	return write_file_replace(path, result) ? FIX_CHANGED : FIX_ERROR;
}

void publish_state(padfix_state_t *state, const pad_aggregator_t &pads)
{
	const XINPUT_GAMEPAD pad = pads.gamepad();
	InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->seq));
	state->connected = pads.connected_slots ? 1 : 0;
	state->connected_slots = pads.connected_slots;
	state->buttons = pad.wButtons;
	state->left_trigger = pad.bLeftTrigger;
	state->right_trigger = pad.bRightTrigger;
	state->thumb_lx = pad.sThumbLX;
	state->thumb_ly = pad.sThumbLY;
	state->thumb_rx = pad.sThumbRX;
	state->thumb_ry = pad.sThumbRY;
	InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->seq));
	InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->heartbeat));
}

void fix_broadcast_in_profiles(const wchar_t *global_controls)
{
	std::wstring root = global_controls;
	const size_t slash = root.find_last_of(L'\\');
	if (slash == std::wstring::npos) return;
	root.resize(slash);
	for (const wchar_t *folder : {L"profiles", L"steam_profiles"}) {
		const std::wstring base = root + L"\\" + folder + L"\\";
		WIN32_FIND_DATAW found;
		HANDLE search = FindFirstFileW((base + L"*").c_str(), &found);
		if (search == INVALID_HANDLE_VALUE) continue;
		do {
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.cFileName[0] != L'.')
				fix_merged_rumble((base + found.cFileName + L"\\controls.sii").c_str(), 0);
		} while (FindNextFileW(search, &found));
		FindClose(search);
	}
}

bool read_rumble_command(const rumble_shared_t *shared, rumble_command_t &out)
{
	for (unsigned i = 0; i < 16; ++i) {
		const uint32_t seq = shared->command.seq;
		MemoryBarrier();
		if (seq & 1) continue;
		memcpy(&out, &shared->command, sizeof(out));
		MemoryBarrier();
		if (shared->command.seq == seq) return true;
	}
	return false;
}

int serve_game(const DWORD game_pid, const wchar_t *const global_controls)
{
	// One helper serves every plugin load in the game session.

	HANDLE mutex = CreateMutexW(NULL, TRUE, PADFIX_MUTEX_NAME);
	if (! mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
		return 0;
	}

	HANDLE game = game_pid ? OpenProcess(SYNCHRONIZE, FALSE, game_pid) : NULL;
	if (! game) {
		return 1;
	}

	HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(padfix_state_t), PADFIX_SHM_NAME);
	if (! mapping) {
		return 1;
	}
	padfix_state_t *const state = static_cast<padfix_state_t *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(padfix_state_t)));
	if (! state) {
		return 1;
	}
	state->magic = PADFIX_MAGIC;

	timeBeginPeriod(1);

	pad_aggregator_t pads;
	rumble_output_t rumble;
	HANDLE rumble_mapping = NULL;
	rumble_shared_t *rumble_shared = NULL;
	rumble_command_t command = {};
	DWORD classify_at = 0, classified_connected = 0xffffffff;
	DWORD targets = 0, virtuals = 0, unknown = 0;
	bool saw_producer = false;
	DWORD wait_result;

	while ((wait_result = WaitForSingleObject(game, POLL_INTERVAL_MS)) == WAIT_TIMEOUT) {
		pads.poll(GetTickCount());
		publish_state(state, pads);
		const DWORD now = GetTickCount();
		if (classified_connected != pads.connected_slots || static_cast<LONG>(now - classify_at) >= 0) {
			classify_at = now + 500;
			classified_connected = pads.connected_slots;
			pad_source_t sources[XUSER_MAX_COUNT] = {};
			query_pad_sources(sources);
			virtuals = unknown = 0;
			for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
				if (!(pads.connected_slots & (1u << i))) continue;
				if (sources[i] == PAD_VIRTUAL) virtuals |= 1u << i;
				if (sources[i] == PAD_UNKNOWN) unknown |= 1u << i;
			}
			targets = pads.connected_slots & ~virtuals;
			if (!rumble_mapping) {
				rumble_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, PADFIX_RUMBLE_SHM_NAME);
				if (rumble_mapping) rumble_shared = static_cast<rumble_shared_t *>(MapViewOfFile(rumble_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rumble_shared_t)));
				if (rumble_mapping && !rumble_shared) { CloseHandle(rumble_mapping); rumble_mapping = NULL; }
			}
		}
		if (rumble_shared) {
			read_rumble_command(rumble_shared, command);
			if (command.magic == PADFIX_RUMBLE_MAGIC && command.game_pid == game_pid) saw_producer = true;
		}
		DWORD foreground_pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
		rumble.poll(now, targets, command, game_pid, foreground_pid == game_pid);
		if (rumble_shared) {
			auto &status = rumble_shared->status;
			InterlockedIncrement(reinterpret_cast<volatile LONG *>(&status.seq));
			status.target_slots = targets;
			status.virtual_slots = virtuals;
			status.unknown_slots = unknown;
			status.command_id = rumble.last_command;
			status.left_motor = rumble.output.wLeftMotorSpeed;
			status.right_motor = rumble.output.wRightMotorSpeed;
			status.writes = rumble.writes;
			status.blocked = rumble.inhibited ? 1 : 0;
			status.connected_slots = pads.connected_slots;
			status.failed_slots = rumble.failed_slots & targets;
			InterlockedIncrement(reinterpret_cast<volatile LONG *>(&status.seq));
		}
	}

	rumble.stop(XInputSetState);
	if (rumble_shared) {
		auto &status = rumble_shared->status;
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&status.seq));
		status.left_motor = status.right_motor = 0;
		status.blocked = 1;
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&status.seq));
		UnmapViewOfFile(rumble_shared);
	}
	if (rumble_mapping) CloseHandle(rumble_mapping);
	timeEndPeriod(1);
	UnmapViewOfFile(state);
	CloseHandle(mapping);
	CloseHandle(game);

	// The game has written its files by now, so the fix sticks until next launch.

	if (wait_result == WAIT_OBJECT_0 && global_controls && *global_controls) {
		fix_whitelist(global_controls);
		if (saw_producer) fix_broadcast_in_profiles(global_controls);
	}

	ReleaseMutex(mutex);
	CloseHandle(mutex);
	return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	LPWSTR *const argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (! argv || argc < 2) {
		return 1;
	}
	if (wcscmp(argv[1], L"--fix-whitelist") == 0) {
		return (argc >= 3) ? fix_whitelist(argv[2]) : FIX_ERROR;
	}
	if (wcscmp(argv[1], L"--fix-merged-steering") == 0) {
		return argc >= 3 ? fix_merged_steering(argv[2]) : FIX_ERROR;
	}
	if (wcscmp(argv[1], L"--fix-broadcast-rumble") == 0) {
		return argc == 3 ? fix_merged_rumble(argv[2], 0) : FIX_ERROR;
	}
	if (wcscmp(argv[1], L"--fix-merged-rumble") == 0) {
		if (argc != 4 || wcslen(argv[3]) != 1 || argv[3][0] < L'1' || argv[3][0] > L'4') return FIX_ERROR;
		return fix_merged_rumble(argv[2], argv[3][0] - L'1');
	}
	if (wcscmp(argv[1], L"--fix-steering") == 0) {
		DWORD slot = 0;
		if (argc >= 4) {
			if (wcslen(argv[3]) != 1 || argv[3][0] < L'1' || argv[3][0] > L'4') return FIX_ERROR;
			slot = argv[3][0] - L'1';
		}
		return (argc >= 3) ? fix_steering(argv[2], slot) : FIX_ERROR;
	}
	return serve_game(wcstoul(argv[1], NULL, 10), (argc >= 3) ? argv[2] : NULL);
}
