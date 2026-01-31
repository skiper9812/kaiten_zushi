#pragma once
#include "ipc_manager.h"

// Starts the Belt process loop
void startBelt();

// Rotates the belt one step
void rotateBelt(RestaurantState* state);
