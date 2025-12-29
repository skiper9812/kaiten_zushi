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
#include "common.h"

key_t SHM_KEY = -1;
key_t SEM_KEY = -1;

static int shm_id = -1;
static int sem_id = -1;
static RestaurantState* state = NULL;

/* ---------- SEMAFORY ---------- */

static void sem_set(int val) {
    union semun {
        int val;
        struct semid_ds* buf;
        unsigned short* array;
    } arg;

    arg.val = val;
    CHECK_ERR(semctl(sem_id, 0, SETVAL, arg), ERR_SEM_OP, "semctl SETVAL");
}

void sem_lock() {
    struct sembuf op = { 0, -1, 0 };
    CHECK_ERR(semop(sem_id, &op, 1), ERR_SEM_OP, "semop lock");
}

void sem_unlock() {
    struct sembuf op = { 0, 1, 0 };
    CHECK_ERR(semop(sem_id, &op, 1), ERR_SEM_OP, "semop unlock");
}

/* ---------- IPC INIT ---------- */

int ipc_init() {
    SHM_KEY = ftok(".", 'A');
    CHECK_ERR(SHM_KEY, ERR_IPC_INIT, "ftok SHM");

    SEM_KEY = ftok(".", 'B');
    CHECK_ERR(SEM_KEY, ERR_IPC_INIT, "ftok SEM");

    /* SHM */
    shm_id = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0666);
    CHECK_ERR(shm_id, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shm_id, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    /* SEM */
    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    CHECK_ERR(sem_id, ERR_IPC_INIT, "semget");

    sem_set(1);

    /* INIT STATE */
    sem_lock();

    memset(state, 0, sizeof(RestaurantState));

    state->restaurantMode = OPEN;
    state->beltHead = 0;
    state->beltTail = 0;
    state->currentDishCount = 0;

    state->nextGuestID = 1;
    state->nextGroupID = 1;
    state->nextDishID = 1;

    for (int i = 0; i < TABLE_COUNT; ++i) {
        state->tables[i].tableID = i;

        if (i < X1)
            state->tables[i].capacity = 1;
        else if (i < X1 + X2)
            state->tables[i].capacity = 2;
        else if (i < X1 + X2 + X3)
            state->tables[i].capacity = 3;
        else
            state->tables[i].capacity = 4;

        state->tables[i].isOccupied = 0;
        state->tables[i].groupID = -1;
        state->tables[i].groupSize = 0;
    }

    sem_unlock();

    return 0;
}

/* ---------- ACCESS ---------- */

RestaurantState* get_state() {
    return state;
}

/* ---------- CLEANUP ---------- */

void ipc_cleanup() {
    if (state && state != (void*)-1) {
        shmdt(state);
        state = NULL;
    }

    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        shm_id = -1;
    }

    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        sem_id = -1;
    }
}
