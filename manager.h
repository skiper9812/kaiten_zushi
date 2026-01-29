#pragma once
#include "ipc_manager.h"

void startManager();
void handleManagerSignal(int sig);
void sendCloseSignal();
