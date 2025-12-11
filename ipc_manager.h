#pragma once

void ipc_init();
void ipc_cleanup();

void sem_init_all();
void sem_destroy_all();

void shm_init();
void shm_destroy();

void msg_init();
void msg_destroy();

void fifo_init();
void fifo_destroy();
