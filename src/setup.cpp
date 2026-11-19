/**
 * @file setup.cpp
 * @brief padfix_setup.exe - installs padfix into ETS2/ATS or removes it.
 *
 * A native replacement for the old PowerShell installer: a single executable
 * that can be code-signed, so Windows Smart App Control and antivirus software
 * can trust the whole install path (an unsigned script cannot get that trust).
 *
 * Usage:
 *   padfix_setup.exe                          Install into every ETS2/ATS found in the Steam libraries
 *   padfix_setup.exe --game-dir <folder>      Install into this game folder only
 *   padfix_setup.exe --uninstall              Remove padfix (can be combined with --game-dir)
 *   padfix_setup.exe --files-only             Copy the plugin; leave game settings and profiles alone
 *   padfix_setup.exe --documents-dir <folder> Use this folder instead of Documents (for testing)
 *
 * The old install.ps1 spellings -GameDir and -Uninstall are accepted too.
 * Exit code 0 = success, 1 = failure.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <conio.h>
#include <wchar.h>

#include <string>
#include <vector>

#include "version.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

namespace {

struct game_t
{
	const wchar_t *name; // Folder name under steamapps\common and Documents.
	const wchar_t *exe;  // Under bin\win_x64.
};

const game_t GAMES[] = {
	{ L"Euro Truck Simulator 2", L"eurotrucks2.exe" },
	{ L"American Truck Simulator", L"amtrucks.exe" },
};

const wchar_t HELPER_EXE[] = L"padfix_helper.exe";

// padfix_helper.exe --fix-* exit codes.
const int FIX_NOTHING_TO_DO = 0;
const int FIX_CHANGED = 2;

/* Output */

enum color_t { PLAIN, RED, GREEN, YELLOW, CYAN };

HANDLE g_out = INVALID_HANDLE_VALUE;
bool g_console = false;
WORD g_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

void init_output()
{
	g_out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode;
	g_console = GetConsoleMode(g_out, &mode) != FALSE;
	CONSOLE_SCREEN_BUFFER_INFO info;
	if (g_console && GetConsoleScreenBufferInfo(g_out, &info)) {
		g_attributes = info.wAttributes;
	}
}

// Writes one line. Paths can contain any Unicode character, so a console gets
// UTF-16 directly and a redirected stream gets UTF-8.
void print(const std::wstring &text, const color_t color = PLAIN)
{
	if (g_out == INVALID_HANDLE_VALUE || g_out == NULL) {
		return;
	}
	if (g_console) {
		WORD foreground = 0;
		switch (color) {
			case RED: foreground = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
			case GREEN: foreground = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
			case YELLOW: foreground = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
			case CYAN: foreground = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
			default: break;
		}
		if (foreground) {
			SetConsoleTextAttribute(g_out, static_cast<WORD>((g_attributes & 0xF0) | foreground));
		}
		const std::wstring line = text + L"\n";
		DWORD written;
		WriteConsoleW(g_out, line.c_str(), static_cast<DWORD>(line.size()), &written, NULL);
		if (foreground) {
			SetConsoleTextAttribute(g_out, g_attributes);
		}
		return;
	}
	const std::wstring line = text + L"\r\n";
	const int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), NULL, 0, NULL, NULL);
	if (size <= 0) {
		return;
	}
	std::string utf8(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &utf8[0], size, NULL, NULL);
	DWORD written;
	WriteFile(g_out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, NULL);
}

std::wstring error_text(const DWORD error)
{
	wchar_t *buffer = NULL;
	const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, NULL);
	std::wstring text = (length && buffer) ? std::wstring(buffer, length) : std::wstring();
	LocalFree(buffer);
	while (! text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ' || text.back() == L'.')) {
		text.pop_back();
	}
	return L"error " + std::to_wstring(error) + (text.empty() ? std::wstring() : L": " + text);
}

/* Paths and files */

std::wstring join(const std::wstring &base, const std::wstring &name)
{
	if (base.empty()) {
		return name;
	}
	if (base.back() == L'\\' || base.back() == L'/') {
		return base + name;
	}
	return base + L'\\' + name;
}

bool file_exists(const std::wstring &path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && ! (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::wstring &path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Absolute path without a trailing backslash (except for a drive root).
std::wstring full_path(const std::wstring &path)
{
	const DWORD needed = GetFullPathNameW(path.c_str(), 0, NULL, NULL);
	if (needed == 0) {
		return path;
	}
	std::wstring out(needed, L'\0');
	const DWORD length = GetFullPathNameW(path.c_str(), needed, &out[0], NULL);
	if (length == 0 || length >= needed) {
		return path;
	}
	out.resize(length);
	while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/')) {
		out.pop_back();
	}
	return out;
}

// Steam reports the same folder with different letter case (d:\steam, D:\Steam).
bool same_path(const std::wstring &a, const std::wstring &b)
{
	return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

void add_unique(std::vector<std::wstring> &list, const std::wstring &path)
{
	for (const std::wstring &existing : list) {
		if (same_path(existing, path)) {
			return;
		}
	}
	list.push_back(path);
}

std::wstring exe_dir()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(NULL, &path[0], static_cast<DWORD>(path.size()));
		if (length == 0) {
			return std::wstring();
		}
		if (length < path.size()) {
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring documents_dir()
{
	PWSTR path = NULL;
	std::wstring out;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &path)) && path) {
		out = path;
	}
	CoTaskMemFree(path);
	return out;
}

bool read_file(const std::wstring &path, std::string &out)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
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

std::wstring utf8_to_wide(const std::string &text)
{
	if (text.empty()) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
	if (length <= 0) {
		return std::wstring();
	}
	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], length);
	return out;
}

bool make_dir(const std::wstring &path)
{
	return CreateDirectoryW(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool copy_file(const std::wstring &from, const std::wstring &to)
{
	if (CopyFileW(from.c_str(), to.c_str(), FALSE)) {
		return true;
	}
	const DWORD error = GetLastError();
	print(L"  Could not copy " + to + L" (" + error_text(error) + L").", RED);
	if (error == ERROR_ACCESS_DENIED) {
		print(L"  Make sure nothing is using the file, or run padfix_setup.exe as administrator.", YELLOW);
	}
	return false;
}

void remove_tree(const std::wstring &dir)
{
	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW(join(dir, L"*").c_str(), &data);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			const std::wstring name = data.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			const std::wstring path = join(dir, name);
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
					RemoveDirectoryW(path.c_str()); // Remove the link, never its target.
				}
				else {
					remove_tree(path);
				}
			}
			else {
				SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(path.c_str());
			}
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}
	RemoveDirectoryW(dir.c_str());
}

/* Steam libraries and game installs */

// libraryfolders.vdf lists each library as  "path"  "D:\\SteamLibrary"
std::vector<std::wstring> vdf_library_paths(const std::string &vdf)
{
	std::vector<std::wstring> paths;
	const std::string key = "\"path\"";
	size_t pos = 0;
	while ((pos = vdf.find(key, pos)) != std::string::npos) {
		pos += key.size();
		while (pos < vdf.size() && (vdf[pos] == ' ' || vdf[pos] == '\t' || vdf[pos] == '\r' || vdf[pos] == '\n')) {
			++pos;
		}
		if (pos >= vdf.size() || vdf[pos] != '"') {
			continue;
		}
		std::string value;
		for (++pos; pos < vdf.size() && vdf[pos] != '"'; ++pos) {
			if (vdf[pos] == '\\' && pos + 1 < vdf.size()) {
				++pos;
			}
			value += vdf[pos];
		}
		if (! value.empty()) {
			paths.push_back(utf8_to_wide(value));
		}
	}
	return paths;
}

std::vector<std::wstring> steam_libraries()
{
	std::vector<std::wstring> libraries;
	wchar_t steam[MAX_PATH];
	DWORD size = sizeof(steam);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, steam, &size) != ERROR_SUCCESS) {
		return libraries;
	}
	std::wstring root = steam;
	for (wchar_t &c : root) {
		if (c == L'/') {
			c = L'\\';
		}
	}
	add_unique(libraries, full_path(root));
	std::string vdf;
	if (read_file(join(root, L"steamapps\\libraryfolders.vdf"), vdf)) {
		for (const std::wstring &path : vdf_library_paths(vdf)) {
			add_unique(libraries, full_path(path));
		}
	}
	return libraries;
}

struct install_t
{
	const game_t *game;
	std::wstring dir;
};

std::vector<install_t> find_installs(const std::wstring &game_dir)
{
	std::vector<std::wstring> candidates;
	if (! game_dir.empty()) {
		add_unique(candidates, full_path(game_dir));
	}
	else {
		for (const std::wstring &library : steam_libraries()) {
			for (const game_t &game : GAMES) {
				add_unique(candidates, join(library, std::wstring(L"steamapps\\common\\") + game.name));
			}
		}
	}
	std::vector<install_t> installs;
	for (const std::wstring &dir : candidates) {
		for (const game_t &game : GAMES) {
			if (file_exists(join(dir, std::wstring(L"bin\\win_x64\\") + game.exe))) {
				installs.push_back({ &game, dir });
			}
		}
	}
	return installs;
}

/* Processes */

std::vector<DWORD> processes_named(const wchar_t *const exe_name)
{
	std::vector<DWORD> ids;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return ids;
	}
	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
		if (entry.th32ProcessID != GetCurrentProcessId() && _wcsicmp(entry.szExeFile, exe_name) == 0) {
			ids.push_back(entry.th32ProcessID);
		}
	}
	CloseHandle(snapshot);
	return ids;
}

// Stops the helper that runs from this install, so its file can be replaced.
// Helpers of other installs are left alone.
void stop_helper(const std::wstring &helper_path)
{
	for (const DWORD id : processes_named(HELPER_EXE)) {
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id);
		if (! process) {
			continue;
		}
		wchar_t image[MAX_PATH * 2];
		DWORD length = ARRAYSIZE(image);
		if (QueryFullProcessImageNameW(process, 0, image, &length) && same_path(std::wstring(image, length), helper_path)) {
			if (TerminateProcess(process, 0)) {
				WaitForSingleObject(process, 5000);
			}
		}
		CloseHandle(process);
	}
}

// Runs the installed helper with one of its --fix-* options.
// Returns its exit code, or -1 if it could not run.
int run_helper(const std::wstring &helper, const wchar_t *const option, const std::wstring &file)
{
	std::wstring command_line = L"\"" + helper + L"\" " + option + L" \"" + file + L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (! CreateProcessW(helper.c_str(), &command_line[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
		return -1;
	}
	CloseHandle(process.hThread);
	DWORD code = 1;
	if (WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0) {
		GetExitCodeProcess(process.hProcess, &code);
	}
	else {
		TerminateProcess(process.hProcess, 1);
		code = static_cast<DWORD>(-1);
	}
	CloseHandle(process.hProcess);
	return static_cast<int>(code);
}

/* Install / uninstall */

struct options_t
{
	bool uninstall = false;
	bool files_only = false;
	std::wstring game_dir;
	std::wstring documents;
};

bool uninstall_from(const std::wstring &dll, const std::wstring &helper_dir)
{
	DeleteFileW(dll.c_str());
	remove_tree(helper_dir);
	if (file_exists(dll) || dir_exists(helper_dir)) {
		print(L"  Could not remove every padfix file. Close anything that uses them and run this again.", RED);
		return false;
	}
	print(L"  padfix removed.", GREEN);
	print(L"  Remember to bind your controls to your own gamepad again in the game.");
	return true;
}

// The game sometimes hides the device in the controls menu; the helper fixes that.
// It also keeps merged steering on the stable in-game XInput slot used for rumble.
void configure_game(const install_t &install, const std::wstring &helper, const std::wstring &documents)
{
	if (documents.empty()) {
		print(L"  Documents folder not found, skipped the game settings.", YELLOW);
		return;
	}
	const std::wstring game_documents = join(documents, install.game->name);

	const std::wstring global_controls = join(game_documents, L"global_controls.sii");
	if (file_exists(global_controls)) {
		switch (run_helper(helper, L"--fix-whitelist", global_controls)) {
			case FIX_NOTHING_TO_DO: print(L"  Stable Gamepad is not blacklisted."); break;
			case FIX_CHANGED: print(L"  Stable Gamepad was removed from the blacklist and whitelisted.", GREEN); break;
			default: print(L"  Could not read global_controls.sii, skipped this step.", YELLOW); break;
		}
	}

	for (const wchar_t *const profiles : { L"profiles", L"steam_profiles" }) {
		const std::wstring base = join(game_documents, profiles);
		WIN32_FIND_DATAW data;
		HANDLE find = FindFirstFileW(join(base, L"*").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) {
			continue;
		}
		do {
			const std::wstring name = data.cFileName;
			if (! (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name == L"." || name == L"..") {
				continue;
			}
			const std::wstring controls = join(join(base, name), L"controls.sii");
			if (! file_exists(controls)) {
				continue;
			}
			const int code = run_helper(helper, L"--fix-broadcast-rumble", controls);
			if (code == FIX_CHANGED) {
				print(L"  Stable rumble target and merged steering configured.", GREEN);
			}
			else if (code != FIX_NOTHING_TO_DO) {
				print(L"  Custom profile not changed; configure rumble manually if needed.", YELLOW);
			}
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}
}

bool install_into(const install_t &install, const options_t &options, const std::wstring &dll_source,
	const std::wstring &helper_source)
{
	const std::wstring plugins = join(install.dir, L"bin\\win_x64\\plugins");
	const std::wstring dll = join(plugins, L"padfix.dll");
	const std::wstring helper_dir = join(plugins, L"padfix");
	const std::wstring helper = join(helper_dir, HELPER_EXE);

	stop_helper(helper);
	if (options.uninstall) {
		return uninstall_from(dll, helper_dir);
	}

	if (! make_dir(plugins) || ! make_dir(helper_dir)) {
		print(L"  Could not create " + helper_dir + L" (" + error_text(GetLastError()) + L").", RED);
		return false;
	}
	if (! copy_file(dll_source, dll) || ! copy_file(helper_source, helper)) {
		return false;
	}
	print(L"  Plugin copied.", GREEN);

	if (! options.files_only) {
		configure_game(install, helper, options.documents);
	}
	return true;
}

void print_usage()
{
	print(L"Usage:");
	print(L"  padfix_setup.exe                      Install into every ETS2/ATS in your Steam libraries");
	print(L"  padfix_setup.exe --game-dir <folder>  Install into this game folder");
	print(L"  padfix_setup.exe --uninstall          Remove padfix");
	print(L"  padfix_setup.exe --files-only         Copy the plugin without changing game settings or profiles");
}

// Keeps the window open when the user double-clicked the program.
int finish(const int code)
{
	DWORD processes[2];
	if (g_console && GetConsoleProcessList(processes, ARRAYSIZE(processes)) == 1) {
		print(L"");
		print(L"Press any key to close this window.");
		_getwch();
	}
	return code;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	init_output();
	print(L"padfix setup " PADFIX_VERSION_STRING, CYAN);

	options_t options;
	for (int i = 1; i < argc; ++i) {
		const wchar_t *const arg = argv[i];
		const bool has_value = i + 1 < argc;
		if (_wcsicmp(arg, L"--uninstall") == 0 || _wcsicmp(arg, L"-Uninstall") == 0) {
			options.uninstall = true;
		}
		else if (_wcsicmp(arg, L"--files-only") == 0) {
			options.files_only = true;
		}
		else if ((_wcsicmp(arg, L"--game-dir") == 0 || _wcsicmp(arg, L"-GameDir") == 0) && has_value) {
			options.game_dir = argv[++i];
		}
		else if (_wcsicmp(arg, L"--documents-dir") == 0 && has_value) {
			options.documents = argv[++i];
		}
		else if (_wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"-h") == 0 || wcscmp(arg, L"/?") == 0) {
			print_usage();
			return finish(0);
		}
		else {
			print(L"Unknown option: " + std::wstring(arg), RED);
			print_usage();
			return finish(1);
		}
	}
	// "D:\Games\ETS2\" on the command line arrives as D:\Games\ETS2" (the backslash escapes the quote).
	while (! options.game_dir.empty() && (options.game_dir.back() == L'"' || options.game_dir.back() == L' ')) {
		options.game_dir.pop_back();
	}
	if (options.documents.empty()) {
		options.documents = documents_dir();
	}

	const std::wstring source = exe_dir();
	const std::wstring dll_source = join(source, L"files\\padfix.dll");
	const std::wstring helper_source = join(source, L"files\\padfix\\padfix_helper.exe");
	if (! options.uninstall && (! file_exists(dll_source) || ! file_exists(helper_source))) {
		print(L"The plugin files were not found next to padfix_setup.exe:", RED);
		print(L"  " + dll_source);
		print(L"  " + helper_source);
		print(L"Extract the whole zip to a folder and run padfix_setup.exe from there.");
		print(L"If the files were there, your antivirus may have removed them.");
		return finish(1);
	}

	const std::vector<install_t> installs = find_installs(options.game_dir);
	if (installs.empty()) {
		print(L"Game not found.", RED);
		print(L"Pass the game folder yourself:");
		print(L"  padfix_setup.exe --game-dir \"D:\\SteamLibrary\\steamapps\\common\\Euro Truck Simulator 2\"");
		return finish(1);
	}

	bool failed = false;
	for (const install_t &install : installs) {
		print(L"");
		print(L"== " + std::wstring(install.game->name) + L" (" + install.dir + L")", CYAN);
		if (! processes_named(install.game->exe).empty()) {
			print(L"  " + std::wstring(install.game->name) + L" is running. Close the game first, then run this again.", RED);
			failed = true;
			continue;
		}
		if (! install_into(install, options, dll_source, helper_source)) {
			failed = true;
		}
	}

	if (! options.uninstall && ! failed) {
		print(L"");
		print(L"Next steps:", CYAN);
		print(L"  1. In Steam, open the game's Properties > Controller and disable Steam Input.");
		print(L"  2. Start the game and continue past the \"advanced SDK features\" warning.");
		print(L"  3. Options > Controls > Input type: choose \"Keyboard + Stable Gamepad\" and bind your buttons.");
	}
	return finish(failed ? 1 : 0);
}
