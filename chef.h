#pragma once
#include "ipc_manager.h"

// Starts the Chef process loop
void startChef();

// Core cooking logic (places dish on belt)
void chefPutDish(RestaurantState* state, int dish, int target);
