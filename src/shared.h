/**
 * @file shared.h
 * @brief Pad state shared between padfix_helper.exe (writer) and padfix.dll (reader).
 *
 * The helper runs outside the game process and polls XInput there, because
 * inside eurotrucks2.exe some buttons never arrive through XInput.
 */
#pragma once

#include <stdint.h>

#define PADFIX_SHM_NAME     L"Local\\padfix_state_v1"
#define PADFIX_MUTEX_NAME   L"Local\\padfix_helper_v1"
#define PADFIX_MAGIC        0x46444150u // "PADF"

struct padfix_state_t
{
	volatile uint32_t magic;

	// Odd while the helper is writing, even when the snapshot is consistent.
	volatile uint32_t seq;

	// Incremented after every poll so the plugin can tell the helper is alive.
	volatile uint32_t heartbeat;

	uint32_t connected;
	uint32_t slot;

	// Raw XINPUT_GAMEPAD values.
	uint16_t buttons;
	uint8_t left_trigger;
	uint8_t right_trigger;
	int16_t thumb_lx;
	int16_t thumb_ly;
	int16_t thumb_rx;
	int16_t thumb_ry;
};
