#pragma once

// Plain text helpers for controls.sii and global_controls.sii. No Windows APIs,
// so profile conversions built on them can be checked on any platform.

#include <ctype.h>

#include <string>
#include <vector>

// Stable Gamepad as it appears in controls.sii.
const std::string PADFIX_DEVICE = "sdk.padfix";

inline std::string detect_newline(const std::string &data)
{
	return (data.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
}

inline std::vector<std::string> split_lines(const std::string &data, const std::string &newline)
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

inline std::string join_lines(const std::vector<std::string> &lines, const std::string &newline)
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

inline bool parse_device_line(const std::string &text, const size_t index, device_line_t &out)
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

inline bool input_name_char(char ch)
{
	return isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
}
