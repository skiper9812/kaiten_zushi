#include "ipc_manager.h"
#include "error_handler.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>


void ipc_init() {
    sem_init_all();
    shm_init();
    msg_init();
    fifo_init();
}

void ipc_cleanup() {
    fifo_destroy();
    msg_destroy();
    shm_destroy();
    sem_destroy_all();
}
