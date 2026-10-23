/**
 * @file helper.cpp
 * @brief padfix_helper.exe - polls the first connected XInput pad and publishes
 * its state in shared memory for padfix.dll.
 *
 * Usage:
 *   padfix_helper.exe <game pid> [global_controls.sii path]
 *     Serves the game until it exits, then makes sure the padfix device is not
 *     hidden in the controls menu (see fix_whitelist).
 *   padfix_helper.exe --fix-whitelist <global_controls.sii path>
 *     Only does the whitelist fix. Exit code 0 = nothing to do, 2 = fixed, 1 = error.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <xinput.h>
#include <mmsystem.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "shared.h"

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

// Polling an empty XInput slot is slow, so look for a pad only twice a second
// while none is connected.
const DWORD RESCAN_INTERVAL_MS = 500;
const DWORD POLL_INTERVAL_MS = 2;

const int FIX_NOTHING_TO_DO = 0;
const int FIX_ERROR = 1;
const int FIX_CHANGED = 2;

// Entry the game writes for our device in global_controls.sii.
const char DEVICE_ENTRY[] = "\"sdk.padfix\"";

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

	const std::string newline = (data.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
	std::vector<std::string> lines;
	size_t start = 0;
	for (;;) {
		const size_t end = data.find(newline, start);
		if (end == std::string::npos) {
			lines.push_back(data.substr(start));
			break;
		}
		lines.push_back(data.substr(start, end - start));
		start = end + newline.size();
	}

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
	std::string result;
	for (size_t i = 0; i < out.size(); ++i) {
		if (i) {
			result += newline;
		}
		result += out[i];
	}

	CopyFileW(path, (std::wstring(path) + L".padfix.bak").c_str(), FALSE);
	return write_file_replace(path, result) ? FIX_CHANGED : FIX_ERROR;
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

	DWORD slot = XUSER_MAX_COUNT;
	DWORD next_scan = GetTickCount();
	DWORD wait_result;

	while ((wait_result = WaitForSingleObject(game, POLL_INTERVAL_MS)) == WAIT_TIMEOUT) {
		XINPUT_STATE xs = {};
		bool connected = false;

		if (slot < XUSER_MAX_COUNT) {
			connected = (XInputGetState(slot, &xs) == ERROR_SUCCESS);
		}
		if (! connected) {
			slot = XUSER_MAX_COUNT;
			const DWORD now = GetTickCount();
			if (static_cast<LONG>(now - next_scan) >= 0) {
				next_scan = now + RESCAN_INTERVAL_MS;
				for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
					if (XInputGetState(i, &xs) == ERROR_SUCCESS) {
						slot = i;
						connected = true;
						break;
					}
				}
			}
		}

		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->seq));
		state->connected = connected ? 1 : 0;
		state->slot = slot;
		if (connected) {
			state->buttons = xs.Gamepad.wButtons;
			state->left_trigger = xs.Gamepad.bLeftTrigger;
			state->right_trigger = xs.Gamepad.bRightTrigger;
			state->thumb_lx = xs.Gamepad.sThumbLX;
			state->thumb_ly = xs.Gamepad.sThumbLY;
			state->thumb_rx = xs.Gamepad.sThumbRX;
			state->thumb_ry = xs.Gamepad.sThumbRY;
		}
		else {
			state->buttons = 0;
			state->left_trigger = 0;
			state->right_trigger = 0;
			state->thumb_lx = 0;
			state->thumb_ly = 0;
			state->thumb_rx = 0;
			state->thumb_ry = 0;
		}
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->seq));
		InterlockedIncrement(reinterpret_cast<volatile LONG *>(&state->heartbeat));
	}

	timeEndPeriod(1);
	UnmapViewOfFile(state);
	CloseHandle(mapping);
	CloseHandle(game);

	// The game has written its files by now, so the fix sticks until next launch.

	if (wait_result == WAIT_OBJECT_0 && global_controls && *global_controls) {
		fix_whitelist(global_controls);
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
	return serve_game(wcstoul(argv[1], NULL, 10), (argc >= 3) ? argv[2] : NULL);
}
