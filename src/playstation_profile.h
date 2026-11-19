#pragma once

// Moves bindings made on the DirectInput view of a DualShock 4 or DualSense
// onto Stable Gamepad (padfix_helper.exe --fix-playstation-bindings). Plain
// text processing without Windows APIs, so tools/profile_test.cpp can check it
// on any platform.
//
// Two cases are handled:
// - a joy* alias that points at the controller's di8 device, and
// - the state the game leaves behind when the input type is switched to
//   Stable Gamepad: the alias already is sdk.padfix, but the bindings still
//   carry DirectInput names (z, rz, b1..b14, pov1_*, x on the steering axis).

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include "profile_text.h"

// Steering curve set when a conversion moves steering onto lx and the profile
// still has the linear defaults (c_steer_func and c_steer_dz both 0). The game
// computes pow(normalize(abs(j), c_steer_dz), 1 + c_steer_func * 0.5): 1.0 gives
// an exponent of 1.5, and 0.05 ignores the first 5% of stick travel.
const double PS_STEER_FUNC = 1.0;
const double PS_STEER_DZ = 0.05;
// Leave c_steer_func and c_steer_dz alone (the post-session repair uses this).
const double PS_STEER_KEEP = -1.0;

struct ps_profile_options_t
{
	double steer_func = PS_STEER_FUNC;
	double steer_dz = PS_STEER_DZ;
};

struct ps_profile_result_t
{
	bool changed = false;
	bool error = false;
	std::string text;
	std::vector<std::string> report; // one line per dropped or unconverted binding
};

struct ps_name_t
{
	const char *from;
	const char *to;
};

// DirectInput layout shared by DualShock 4 (both versions) and DualSense.
const ps_name_t PS_DINPUT_NAMES[] = {
	{ "x", "lx" }, { "y", "ly" }, { "z", "rx" }, { "rz", "ry" }, { "rx", "lt" }, { "ry", "rt" },
	{ "b1", "x" }, { "b2", "a" }, { "b3", "b" }, { "b4", "y" }, // Square, Cross, Circle, Triangle
	{ "b5", "lb" }, { "b6", "rb" }, { "b7", "lt_press" }, { "b8", "rt_press" },
	{ "b9", "back" }, { "b10", "start" }, { "b11", "ls" }, { "b12", "rs" },
	{ "pov1_up", "dpad_up" }, { "pov1_down", "dpad_down" }, { "pov1_left", "dpad_left" }, { "pov1_right", "dpad_right" },
	{ "up", "lstick_up" }, { "down", "lstick_down" }, { "left", "lstick_left" }, { "right", "lstick_right" },
};

const char *const PADFIX_AXES[] = { "lx", "ly", "rx", "ry", "lt", "rt" };
const char *const PADFIX_BUTTONS[] = {
	"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs",
	"dpad_up", "dpad_down", "dpad_left", "dpad_right", "lt_press", "rt_press",
	"lstick_up", "lstick_down", "lstick_left", "lstick_right",
	"rstick_up", "rstick_down", "rstick_left", "rstick_right",
};

inline bool padfix_button(const std::string &name)
{
	for (const char *const button : PADFIX_BUTTONS) {
		if (name == button) return true;
	}
	return false;
}

inline bool padfix_input(const std::string &name)
{
	for (const char *const axis : PADFIX_AXES) {
		if (name == axis) return true;
	}
	return padfix_button(name);
}

// Padfix name for a DirectInput name, "" when the input has no equivalent
// (b13 PS, b14 touchpad, b15 DualSense mute), NULL when the name is unknown.
inline const char *ps_dinput_name(const std::string &name)
{
	for (const ps_name_t &entry : PS_DINPUT_NAMES) {
		if (name == entry.from) return entry.to;
	}
	if (name.size() >= 3 && name.size() <= 4 && name[0] == 'b') {
		for (size_t i = 1; i < name.size(); ++i) {
			if (! isdigit(static_cast<unsigned char>(name[i]))) return NULL;
		}
		const int number = atoi(name.c_str() + 1);
		if (number >= 13 && number <= 32) return "";
	}
	return NULL;
}

inline bool ps_hex4(const std::string &text, const size_t pos, unsigned &out)
{
	if (pos + 4 > text.size()) return false;
	out = 0;
	for (size_t i = pos; i < pos + 4; ++i) {
		const char c = static_cast<char>(tolower(static_cast<unsigned char>(text[i])));
		if (c >= '0' && c <= '9') out = out * 16 + static_cast<unsigned>(c - '0');
		else if (c >= 'a' && c <= 'f') out = out * 16 + static_cast<unsigned>(c - 'a' + 10);
		else return false;
	}
	return true;
}

// di8 device whose product GUID {PPPPVVVV-0000-0000-0000-504944564944} names a
// Sony DualShock 4 (05C4, 09CC) or DualSense (0CE6, Edge 0DF2).
inline bool is_playstation_dinput(const std::string &device)
{
	if (device.compare(0, 4, "di8.") != 0) return false;
	std::string lower = device;
	for (char &c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	const size_t suffix = lower.find("-0000-0000-0000-504944564944}");
	unsigned product = 0, vendor = 0;
	if (suffix == std::string::npos || suffix < 9 || lower[suffix - 9] != '{'
		|| ! ps_hex4(lower, suffix - 8, product) || ! ps_hex4(lower, suffix - 4, vendor)) {
		return false;
	}
	return vendor == 0x054C && (product == 0x05C4 || product == 0x09CC || product == 0x0CE6 || product == 0x0DF2);
}

/**
 * @brief A "<kind> <name> `<expression>`" line, e.g. "input j_steer `joy.x`".
 */
struct sii_binding_t
{
	size_t line;
	std::string kind;
	std::string name;
	size_t start; // expression inside the line
	size_t end;
};

inline bool parse_binding_line(const std::string &text, const size_t index, sii_binding_t &out)
{
	const size_t quote = text.find('"');
	const size_t space = (quote == std::string::npos) ? std::string::npos : text.find(' ', quote + 1);
	const size_t open = (quote == std::string::npos) ? std::string::npos : text.find('`', quote + 1);
	const size_t close = (open == std::string::npos) ? std::string::npos : text.find('`', open + 1);
	if (space == std::string::npos || close == std::string::npos || space > open) {
		return false;
	}
	out.line = index;
	out.kind = text.substr(quote + 1, space - quote - 1);
	out.name = text.substr(space + 1, open - space - 1);
	while (! out.name.empty() && out.name.back() == ' ') out.name.pop_back();
	out.start = open + 1;
	out.end = close;
	return true;
}

// Axis inputs a pad's sticks and triggers drive. On padfix, x and y are buttons,
// so x or y on one of these lines can only be a DirectInput name.
inline bool ps_axis_input(const sii_binding_t &binding)
{
	return binding.kind == "input" && (binding.name == "j_steer" || binding.name == "j_throttle"
		|| binding.name == "j_brake" || binding.name == "j_clutch" || binding.name.compare(0, 7, "j_look_") == 0);
}

// Pedals: DirectInput rx/ry are the analog L2/R2 on a PlayStation pad.
inline bool ps_pedal_input(const sii_binding_t &binding)
{
	return binding.kind == "input" && (binding.name == "j_throttle" || binding.name == "j_brake" || binding.name == "j_clutch");
}

/**
 * @brief One "<alias>.<name>[?N]" reference inside an expression.
 */
struct sii_reference_t
{
	size_t start;
	size_t name_start;
	size_t name_end;
	size_t term_end; // after an optional "?N"
	std::string alias;
	std::string name;
};

inline std::vector<sii_reference_t> find_references(const std::string &expression, const std::vector<std::string> &aliases)
{
	std::vector<sii_reference_t> refs;
	for (size_t pos = 0; pos < expression.size(); ++pos) {
		if (pos && input_name_char(expression[pos - 1])) continue;
		for (const std::string &alias : aliases) {
			if (expression.compare(pos, alias.size() + 1, alias + ".") != 0) continue;
			sii_reference_t ref;
			ref.start = pos;
			ref.name_start = pos + alias.size() + 1;
			ref.name_end = ref.name_start;
			while (ref.name_end < expression.size() && (isalnum(static_cast<unsigned char>(expression[ref.name_end])) || expression[ref.name_end] == '_')) {
				++ref.name_end;
			}
			if (ref.name_end == ref.name_start || (ref.name_end < expression.size() && expression[ref.name_end] == '.')) break;
			ref.term_end = ref.name_end;
			if (ref.term_end < expression.size() && expression[ref.term_end] == '?') {
				++ref.term_end;
				while (ref.term_end < expression.size() && isdigit(static_cast<unsigned char>(expression[ref.term_end]))) ++ref.term_end;
			}
			ref.alias = alias;
			ref.name = expression.substr(ref.name_start, ref.name_end - ref.name_start);
			refs.push_back(ref);
			pos = ref.name_end - 1;
			break;
		}
	}
	return refs;
}

// Removes a reference that is one alternative of an "a | b | c" expression.
// Returns false (and changes nothing) when it is part of anything else.
inline bool drop_term(std::string &expression, const sii_reference_t &ref)
{
	const std::string separator = " | ";
	if (expression.compare(ref.term_end, separator.size(), separator) == 0 && (ref.start == 0 || expression.compare(ref.start - separator.size(), separator.size(), separator) == 0)) {
		expression.erase(ref.start, ref.term_end + separator.size() - ref.start);
		return true;
	}
	if (ref.term_end == expression.size() && ref.start >= separator.size() && expression.compare(ref.start - separator.size(), separator.size(), separator) == 0) {
		expression.erase(ref.start - separator.size(), ref.term_end - ref.start + separator.size());
		return true;
	}
	return false;
}

// Finds "constant <name> <value>" and the value's position in its line.
inline bool find_constant(const std::vector<std::string> &lines, const std::string &name, size_t &line, size_t &start, size_t &end)
{
	const std::string key = "\"constant " + name + " ";
	for (size_t i = 0; i < lines.size(); ++i) {
		const size_t pos = lines[i].find(key);
		if (pos == std::string::npos) continue;
		start = pos + key.size();
		end = lines[i].find('"', start);
		if (end == std::string::npos) return false;
		line = i;
		return true;
	}
	return false;
}

inline bool constant_is_zero(const std::vector<std::string> &lines, const std::string &name)
{
	size_t line = 0, start = 0, end = 0;
	if (! find_constant(lines, name, line, start, end)) return false;
	const std::string value = lines[line].substr(start, end - start);
	char *stop = NULL;
	const double number = strtod(value.c_str(), &stop);
	return stop != value.c_str() && *stop == '\0' && number == 0.0;
}

inline bool set_constant(std::vector<std::string> &lines, const std::string &name, const double value)
{
	size_t line = 0, start = 0, end = 0;
	if (! find_constant(lines, name, line, start, end)) return false;
	char text[32];
	snprintf(text, sizeof(text), "%.6f", value);
	if (lines[line].compare(start, end - start, text) == 0) return false;
	lines[line].replace(start, end - start, text);
	return true;
}

inline ps_profile_result_t convert_playstation_profile(const std::string &data, const ps_profile_options_t &options = ps_profile_options_t())
{
	ps_profile_result_t result;
	result.text = data;
	const std::string newline = detect_newline(data);
	std::vector<std::string> lines = split_lines(data, newline);

	std::vector<device_line_t> sony;
	device_line_t padfix;
	bool has_padfix = false;
	std::vector<sii_binding_t> bindings;
	for (size_t i = 0; i < lines.size(); ++i) {
		device_line_t device;
		sii_binding_t binding;
		if (parse_device_line(lines[i], i, device)) {
			if (device.alias.compare(0, 3, "joy") != 0) continue; // keyboard, mouse
			if (device.device == PADFIX_DEVICE && ! has_padfix) {
				padfix = device;
				has_padfix = true;
			}
			else if (is_playstation_dinput(device.device)) {
				sony.push_back(device);
			}
		}
		else if (parse_binding_line(lines[i], i, binding)) {
			bindings.push_back(binding);
		}
	}

	// A padfix alias is stale when it carries names that only exist on the
	// DirectInput device. rx/ry alone are no evidence: padfix has them too
	// (right stick), and a correct profile looks around with them.
	bool stale = false;
	if (has_padfix) {
		const std::vector<std::string> alias(1, padfix.alias);
		for (const sii_binding_t &binding : bindings) {
			const std::string expression = lines[binding.line].substr(binding.start, binding.end - binding.start);
			for (const sii_reference_t &ref : find_references(expression, alias)) {
				if ((! padfix_input(ref.name) && ps_dinput_name(ref.name)) || ((ref.name == "x" || ref.name == "y") && ps_axis_input(binding))) {
					stale = true;
				}
			}
		}
	}
	if (sony.empty() && ! stale) {
		return result;
	}

	std::string target;
	if (has_padfix) {
		target = padfix.alias;
	}
	else {
		target = sony[0].alias;
		for (const device_line_t &device : sony) {
			if (device.alias == "joy") target = device.alias;
		}
	}
	std::vector<std::string> aliases;
	for (const device_line_t &device : sony) aliases.push_back(device.alias);
	if (has_padfix) aliases.push_back(padfix.alias);

	auto is_dinput = [&](const sii_reference_t &ref, const sii_binding_t &binding) {
		if (! has_padfix || ref.alias != padfix.alias) return true; // a Sony alias
		if (! stale) return false;
		if (! padfix_input(ref.name)) return true;
		if (ref.name == "x" || ref.name == "y") return ps_axis_input(binding);
		if (ref.name == "rx" || ref.name == "ry") return ps_pedal_input(binding);
		return false;
	};

	// Buttons already bound on padfix, to avoid one button doing two things.
	std::vector<std::pair<std::string, std::string>> bound; // button, mix
	for (const sii_binding_t &binding : bindings) {
		if (binding.kind != "mix" || ! has_padfix) continue;
		const std::string expression = lines[binding.line].substr(binding.start, binding.end - binding.start);
		for (const sii_reference_t &ref : find_references(expression, aliases)) {
			if (ref.alias == padfix.alias && ! is_dinput(ref, binding) && padfix_button(ref.name)) {
				bound.push_back(std::make_pair(ref.name, binding.name));
			}
		}
	}

	std::vector<std::string> unknown;
	std::vector<std::string> pedal_constants;
	bool steer_to_lx = false;
	for (const sii_binding_t &binding : bindings) {
		std::string expression = lines[binding.line].substr(binding.start, binding.end - binding.start);
		const std::vector<sii_reference_t> refs = find_references(expression, aliases);
		const bool gear = binding.kind == "mix" && binding.name.size() == 5 && binding.name.compare(0, 4, "gear") == 0
			&& binding.name[4] >= '1' && binding.name[4] <= '7';
		const std::string where = binding.kind + " " + binding.name + ": ";
		for (size_t r = refs.size(); r-- > 0;) {
			const sii_reference_t &ref = refs[r];
			if (! is_dinput(ref, binding)) continue;
			const char *const to = ps_dinput_name(ref.name);
			if (! to) {
				unknown.push_back(ref.alias + "." + ref.name);
				continue;
			}
			const std::string old_name = ref.alias + "." + ref.name;
			const std::string new_name = target + "." + to;
			// The game's default gear1..7 pad buttons would also be stick clicks,
			// Back and Start on padfix: drop them, as convert_to_padfix.py does.
			bool drop = ! *to || (gear && ref.name[0] == 'b');
			std::string reason;
			if (! *to && ! gear) {
				reason = old_name + " has no Stable Gamepad input (PS button, touchpad, mute), dropped";
			}
			if (! drop && binding.kind == "mix" && padfix_button(to)) {
				for (const auto &entry : bound) {
					if (entry.first != to) continue;
					drop = true;
					if (entry.second != binding.name) {
						reason = old_name + " would become " + new_name + ", which mix " + entry.second + " already uses; dropped";
					}
					break;
				}
			}
			if (drop) {
				if (! drop_term(expression, ref)) {
					result.report.push_back(where + old_name + " left unchanged (not a plain alternative); rebind it in the game");
					continue;
				}
				if (! reason.empty()) result.report.push_back(where + reason);
				continue;
			}
			expression.replace(ref.start, ref.name_end - ref.start, new_name);
			if (binding.kind == "input" && binding.name == "j_steer" && std::string(to) == "lx") {
				steer_to_lx = true;
			}
			if (ps_pedal_input(binding) && (std::string(to) == "lt" || std::string(to) == "rt")) {
				pedal_constants.push_back("c_jz" + binding.name.substr(2));
			}
		}
		lines[binding.line].replace(binding.start, binding.end - binding.start, expression);
	}
	if (! unknown.empty()) {
		std::string names;
		for (const std::string &name : unknown) names += (names.empty() ? "" : ", ") + name;
		result.report.assign(1, "unknown inputs, nothing written: " + names);
		result.error = true;
		return result;
	}

	for (const device_line_t &device : sony) {
		const std::string value = (device.alias == target) ? PADFIX_DEVICE : std::string();
		lines[device.line].replace(device.value_start, device.value_end - device.value_start, value);
	}
	// lt/rt are 0..1; with c_jz* at 0 the game reads the axis as -1..1 and the
	// pedal rests half pressed.
	for (const std::string &name : pedal_constants) {
		if (set_constant(lines, name, 1.0)) result.report.push_back("constant " + name + " set to 1 (pedal on a 0..1 trigger)");
	}
	if (steer_to_lx && options.steer_func >= 0 && options.steer_dz >= 0
		&& constant_is_zero(lines, "c_steer_func") && constant_is_zero(lines, "c_steer_dz")) {
		set_constant(lines, "c_steer_func", options.steer_func);
		set_constant(lines, "c_steer_dz", options.steer_dz);
		char text[96];
		snprintf(text, sizeof(text), "steering curve for a stick: c_steer_func %.6f, c_steer_dz %.6f", options.steer_func, options.steer_dz);
		result.report.push_back(text);
	}

	result.text = join_lines(lines, newline);
	result.changed = result.text != data;
	return result;
}
