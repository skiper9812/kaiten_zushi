#pragma once
#include "common.h"
extern RestaurantState* get_state();
void handle_manager_signal(int sig);

int ipc_init();
void ipc_cleanup();

static void sem_set(int semnum, int val);
void P(int semnum);
void V(int semnum);

void send_premium_order(int table_id, int price);
int recv_premium_order(struct PremiumMsg* msg);

void sem_init_all();
void sem_destroy_all();

void shm_init();
void shm_destroy();

void fifo_init();
void fifo_open_write();
void fifo_close_write();
void fifo_log(const char* msg);
void logger_loop(const char* filename);
void fifo_init_close_signal();