#pragma once
#include "shared.h"

// Hooks are scoped to the game process. Slot 1 is a stable merged XInput facade;
// raw input and real motor writes remain in the external helper process.
bool rumble_bridge_start(const padfix_state_t *input);
void rumble_bridge_tick(bool input_alive);
void rumble_bridge_set_driving(bool driving);
void rumble_bridge_stop();
const char *rumble_bridge_error();
