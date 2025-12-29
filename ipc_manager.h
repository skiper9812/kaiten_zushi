#pragma once
#include "error_handler.h"
#include "common.h"
RestaurantState* get_state();

int ipc_init();
void ipc_cleanup();

void sem_set();
void sem_lock();
void sem_unlock();

void sem_init_all();
void sem_destroy_all();

void shm_init();
void shm_destroy();
