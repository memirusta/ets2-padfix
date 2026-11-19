#pragma once

// Reads PlayStation controllers directly over HID, next to XInput, so they join
// Stable Gamepad without Steam Input or DS4Windows. Devices are opened shared:
// Steam and other programs keep working with them. A controller another program
// opened exclusively (or hid from other programs) is simply skipped.

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <wchar.h>
#include <wctype.h>

#include <memory>
#include <string>
#include <vector>

#include "controller_merge.h"
#include "playstation_pad.h"

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

// A controller that sent nothing for this long counts as gone (e.g. a Bluetooth
// link that dropped before Windows removed the device). In their full report
// mode both families report continuously, many times per second.
const DWORD PS_STALE_MS = 1000;
const DWORD PS_SCAN_INTERVAL_MS = 2000;
const size_t PS_MAX_PADS = 4;

struct ps_device_t
{
	std::wstring path;
	HANDLE handle = INVALID_HANDLE_VALUE;
	ps_model_t model = PS_NONE;
	bool bluetooth = false;
	bool writable = false;
	USHORT output_length = 0;
	std::vector<BYTE> input;
	OVERLAPPED read = {};
	bool reading = false;
	bool failed = false;
	bool has_report = false;
	DWORD last_report = 0;
	XINPUT_GAMEPAD state = {};
	ps_rumble_state_t rumble;
	BYTE sequence = 0;

	ps_device_t() = default;
	ps_device_t(const ps_device_t &) = delete;
	ps_device_t &operator=(const ps_device_t &) = delete;
	~ps_device_t() { close(); }

	void close()
	{
		if (handle != INVALID_HANDLE_VALUE) {
			if (reading) {
				// The buffer and OVERLAPPED must outlive the read, so wait for the cancel.
				CancelIoEx(handle, &read);
				DWORD ignored = 0;
				GetOverlappedResult(handle, &read, &ignored, TRUE);
				reading = false;
			}
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}
		if (read.hEvent) {
			CloseHandle(read.hEvent);
			read.hEvent = NULL;
		}
	}

	bool fresh(const DWORD now) const
	{
		return has_report && ! failed && now - last_report < PS_STALE_MS;
	}

	void take_report(const DWORD now, const DWORD size)
	{
		XINPUT_GAMEPAD parsed;
		if (ps_parse_report(model, bluetooth, input.data(), size, parsed)) {
			state = parsed;
			has_report = true;
			last_report = now;
		}
	}

	// Drains finished reads without blocking; keeps one read outstanding.
	void pump(const DWORD now)
	{
		for (unsigned i = 0; i < 16 && ! failed; ++i) {
			DWORD size = 0;
			if (! reading) {
				ResetEvent(read.hEvent);
				if (ReadFile(handle, input.data(), static_cast<DWORD>(input.size()), &size, &read)) {
					take_report(now, size);
					continue;
				}
				if (GetLastError() != ERROR_IO_PENDING) {
					failed = true;
					break;
				}
				reading = true;
			}
			if (! GetOverlappedResult(handle, &read, &size, FALSE)) {
				if (GetLastError() != ERROR_IO_INCOMPLETE) failed = true;
				break;
			}
			reading = false;
			take_report(now, size);
		}
	}

	bool write(const BYTE *const report, const size_t size)
	{
		// Windows wants the length of the longest output report, padded with zeros.
		std::vector<BYTE> buffer(size > output_length ? size : output_length, 0);
		memcpy(buffer.data(), report, size);
		OVERLAPPED overlapped = {};
		overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (! overlapped.hEvent) return false;
		DWORD written = 0;
		bool ok = WriteFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, &overlapped) != FALSE;
		if (! ok && GetLastError() == ERROR_IO_PENDING) {
			// Never stall the input loop on a slow Bluetooth link.
			if (WaitForSingleObject(overlapped.hEvent, 50) != WAIT_OBJECT_0) CancelIoEx(handle, &overlapped);
			ok = GetOverlappedResult(handle, &overlapped, &written, TRUE) != FALSE;
		}
		CloseHandle(overlapped.hEvent);
		return ok;
	}

	bool send_rumble(const WORD left, const WORD right)
	{
		BYTE report[PS_RUMBLE_REPORT_MAX];
		const size_t size = ps_rumble_report(model, bluetooth, left, right, sequence, report, sizeof(report));
		sequence = static_cast<BYTE>((sequence + 1) & 0x0F);
		return size && write(report, size);
	}
};

inline bool ps_same_path(const std::wstring &a, const std::wstring &b)
{
	return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// Opens one HID interface if it is a supported controller's gamepad collection.
inline std::unique_ptr<ps_device_t> ps_open(const std::wstring &path)
{
	const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
	bool writable = true;
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		writable = false;
		handle = CreateFileW(path.c_str(), GENERIC_READ, share, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	}
	if (handle == INVALID_HANDLE_VALUE) return nullptr;

	HIDD_ATTRIBUTES attributes = {};
	attributes.Size = sizeof(attributes);
	const ps_model_t model = HidD_GetAttributes(handle, &attributes) ? ps_model(attributes.VendorID, attributes.ProductID) : PS_NONE;
	PHIDP_PREPARSED_DATA preparsed = NULL;
	HIDP_CAPS caps = {};
	const bool described = model != PS_NONE && HidD_GetPreparsedData(handle, &preparsed)
		&& HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
	if (preparsed) HidD_FreePreparsedData(preparsed);
	// Usage page 1 (generic desktop), usage 4/5 (joystick/gamepad): the controller's
	// input collection, not its audio or touchpad interfaces.
	if (! described || caps.UsagePage != 0x01 || (caps.Usage != 0x04 && caps.Usage != 0x05) || caps.InputReportByteLength < 10) {
		CloseHandle(handle);
		return nullptr;
	}

	std::unique_ptr<ps_device_t> device(new ps_device_t());
	device->path = path;
	device->handle = handle;
	device->model = model;
	device->writable = writable;
	device->output_length = caps.OutputReportByteLength;
	device->input.resize(caps.InputReportByteLength);
	// USB reports are 64 bytes on both families; Bluetooth interfaces declare more.
	std::wstring lower = path;
	for (wchar_t &c : lower) c = towlower(c);
	device->bluetooth = caps.InputReportByteLength > 64 || lower.find(L"{00001124-") != std::wstring::npos;
	device->read.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (! device->read.hEvent) return nullptr;

	if (device->bluetooth && caps.FeatureReportByteLength > 1) {
		// Reading the calibration report switches a Bluetooth controller from its
		// basic report to the full one, which is sent continuously (as Steam does).
		std::vector<BYTE> feature(caps.FeatureReportByteLength, 0);
		feature[0] = 0x05;
		HidD_GetFeature(handle, feature.data(), static_cast<ULONG>(feature.size()));
	}
	return device;
}

struct ps_pads_t
{
	std::vector<std::unique_ptr<ps_device_t>> devices;
	DWORD next_scan = 0;
	bool scanned = false;

	bool is_open(const std::wstring &path) const
	{
		for (const auto &device : devices) {
			if (ps_same_path(device->path, path)) return true;
		}
		return false;
	}

	void scan()
	{
		GUID hid;
		HidD_GetHidGuid(&hid);
		HDEVINFO info = SetupDiGetClassDevsW(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (info == INVALID_HANDLE_VALUE) return;
		for (DWORD i = 0; devices.size() < PS_MAX_PADS * 2; ++i) {
			SP_DEVICE_INTERFACE_DATA iface = {};
			iface.cbSize = sizeof(iface);
			if (! SetupDiEnumDeviceInterfaces(info, NULL, &hid, i, &iface)) break;
			DWORD required = 0;
			SetupDiGetDeviceInterfaceDetailW(info, &iface, NULL, 0, &required, NULL);
			if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 65536) continue;
			std::vector<BYTE> buffer(required);
			auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
			detail->cbSize = sizeof(*detail);
			if (! SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, NULL, NULL)) continue;
			std::wstring path = detail->DevicePath;
			std::wstring lower = path;
			for (wchar_t &c : lower) c = towlower(c);
			// USB: vid_054c, Bluetooth: vid&0002054c. Skip everything else unopened.
			if (lower.find(L"054c") == std::wstring::npos || is_open(path)) continue;
			std::unique_ptr<ps_device_t> device = ps_open(path);
			if (device) devices.push_back(std::move(device));
		}
		SetupDiDestroyDeviceInfoList(info);
	}

	void poll(const DWORD now)
	{
		if (! scanned || static_cast<LONG>(now - next_scan) >= 0) {
			scanned = true;
			next_scan = now + PS_SCAN_INTERVAL_MS;
			scan();
		}
		for (auto &device : devices) device->pump(now);
		for (size_t i = 0; i < devices.size();) {
			if (devices[i]->failed) devices.erase(devices.begin() + static_cast<ptrdiff_t>(i));
			else ++i;
		}
	}

	// Bit i = i-th connected PlayStation pad that is currently reporting.
	DWORD mask(const DWORD now) const
	{
		DWORD bits = 0;
		for (size_t i = 0; i < devices.size() && i < PS_MAX_PADS; ++i) {
			if (devices[i]->fresh(now)) bits |= 1u << i;
		}
		return bits;
	}

	void add_to(pad_merge_t &merge, const DWORD now) const
	{
		for (const auto &device : devices) {
			if (device->fresh(now)) merge.add(device->state);
		}
	}

	// Keeps every reporting controller's motors at the game's current command.
	void rumble(const DWORD now, const WORD left, const WORD right)
	{
		for (auto &device : devices) {
			if (! device->writable || ! device->fresh(now) || ! device->rumble.due(now, left, right)) continue;
			device->rumble.sent(now, left, right, device->send_rumble(left, right));
		}
	}

	void stop()
	{
		for (auto &device : devices) {
			if (device->writable && device->rumble.known && (device->rumble.left || device->rumble.right)) {
				device->send_rumble(0, 0);
			}
		}
		devices.clear();
	}
};
