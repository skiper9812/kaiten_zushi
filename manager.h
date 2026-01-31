#pragma once
#include "ipc_manager.h"

// Starts the Manager process
void startManager();

// (Internal) Signal handler
void handleManagerSignal(int sig);

