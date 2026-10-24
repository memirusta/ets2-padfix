#pragma once

#include <windows.h>
#include <xinput.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wchar.h>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

enum pad_source_t { PAD_UNKNOWN, PAD_PHYSICAL, PAD_VIRTUAL };
struct pad_identity_t { WORD vendor; WORD product; pad_source_t source; };

inline pad_source_t match_pad_source(WORD vendor, WORD product, const std::vector<pad_identity_t> &devices)
{
	pad_source_t result = PAD_UNKNOWN;
	bool found = false;
	for (const auto &device : devices) {
		if (device.vendor != vendor || device.product != product) continue;
		if (device.source == PAD_UNKNOWN || (found && result != device.source)) return PAD_UNKNOWN;
		result = device.source;
		found = true;
	}
	return result;
}

inline pad_source_t parent_source(DEVINST node)
{
	// Start at the parent: a virtual controller can itself claim a USB ID.
	for (unsigned depth = 0; depth < 16; ++depth) {
		DEVINST parent;
		if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
		wchar_t id[MAX_DEVICE_ID_LEN] = {};
		if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
		if (_wcsnicmp(id, L"ROOT\\", 5) == 0) return PAD_VIRTUAL;
		if (_wcsnicmp(id, L"USB\\", 4) == 0 || _wcsnicmp(id, L"PCI\\", 4) == 0
			|| _wcsnicmp(id, L"BTH", 3) == 0) return PAD_PHYSICAL;
		node = parent;
	}
	return PAD_UNKNOWN;
}

inline void query_pad_sources(pad_source_t *sources)
{
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) sources[i] = PAD_UNKNOWN;
	// Optional identity query, also used by SDL. Never require ordinal 108:
	// absent/failed queries and ambiguous VID/PID pairs retain activity fallback.
	// https://github.com/libsdl-org/SDL/blob/main/src/core/windows/SDL_xinput.h
	struct capabilities_ex_t {
		XINPUT_CAPABILITIES capabilities;
		WORD vendor, product, revision, reserved;
		DWORD reserved2;
	};
	static_assert(sizeof(capabilities_ex_t) == 32, "XInput capabilities ABI");
	typedef DWORD (WINAPI *caps_fn_t)(DWORD, DWORD, DWORD, capabilities_ex_t *);
	const HMODULE xinput = GetModuleHandleW(L"xinput1_4.dll");
	const auto get_caps = xinput ? reinterpret_cast<caps_fn_t>(GetProcAddress(xinput, MAKEINTRESOURCEA(108))) : nullptr;
	if (!get_caps) return;

	const GUID xusb = {0xec87f1e3, 0xc13b, 0x4100, {0xb5, 0xf7, 0x8b, 0x84, 0xd5, 0x42, 0x60, 0xcb}};
	HDEVINFO info = SetupDiGetClassDevsW(&xusb, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (info == INVALID_HANDLE_VALUE) return;
	std::vector<pad_identity_t> devices;
	for (DWORD i = 0;; ++i) {
		SP_DEVICE_INTERFACE_DATA iface = {};
		iface.cbSize = sizeof(iface);
		if (!SetupDiEnumDeviceInterfaces(info, nullptr, &xusb, i, &iface)) break;
		DWORD required = 0;
		SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, nullptr);
		if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 65536) continue;
		std::vector<BYTE> buffer(required);
		auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
		detail->cbSize = sizeof(*detail);
		SP_DEVINFO_DATA data = {};
		data.cbSize = sizeof(data);
		if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, &data)) continue;
		wchar_t id[MAX_DEVICE_ID_LEN] = {};
		if (CM_Get_Device_IDW(data.DevInst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) continue;
		_wcsupr_s(id);
		const wchar_t *vid = wcsstr(id, L"VID_");
		const wchar_t *pid = wcsstr(id, L"PID_");
		if (!vid || !pid) continue;
		const WORD vendor = static_cast<WORD>(wcstoul(vid + 4, nullptr, 16));
		const WORD product = static_cast<WORD>(wcstoul(pid + 4, nullptr, 16));
		devices.push_back({vendor, product, parent_source(data.DevInst)});
	}
	SetupDiDestroyDeviceInfoList(info);
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		capabilities_ex_t caps = {};
		if (get_caps(1, i, 0, &caps) == ERROR_SUCCESS) {
			sources[i] = match_pad_source(caps.vendor, caps.product, devices);
		}
	}
}
