#pragma once
#include <stdint.h>

#ifdef PADFIX_RUMBLE_TEST
#define PADFIX_RUMBLE_SHM_NAME L"Local\\padfix_rumble_test_v1"
#else
#define PADFIX_RUMBLE_SHM_NAME L"Local\\padfix_rumble_v1"
#endif
#define PADFIX_RUMBLE_MAGIC 0x424d5552u // RUMB

// The plugin writes command; the helper writes status. Separate sequence locks.
struct rumble_command_t {
	uint32_t magic;
	volatile uint32_t seq;
	uint32_t game_pid;
	uint32_t alive_ms;
	uint32_t command_id;
	uint32_t enabled;
	uint16_t left_motor;
	uint16_t right_motor;
};
struct rumble_status_t {
	volatile uint32_t seq;
	uint32_t target_slots;
	uint32_t virtual_slots;
	uint32_t unknown_slots;
	uint32_t command_id;
	uint16_t left_motor;
	uint16_t right_motor;
	uint32_t writes;
	uint32_t blocked;
	uint32_t connected_slots;
	uint32_t failed_slots;
};
struct rumble_shared_t { rumble_command_t command; rumble_status_t status; };
static_assert(sizeof(rumble_shared_t) == 68, "rumble IPC ABI");
