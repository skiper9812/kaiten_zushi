#include "manager.h"

volatile sig_atomic_t restaurantModeFlag = 0;

void handle_manager_signal(int sig) {
    switch (sig) { // 1 - faster, 2 - slower, 3 - evacuate
    case SIGUSR1:
        restaurantModeFlag = 1;
        break;
    case SIGUSR2:
        restaurantModeFlag = 2;
        break;
    default:
        restaurantModeFlag = 3;
        break;
    }
}

void send_close_signal() {
    int fd = open(CLOSE_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;  // brak czytelników jeszcze
    const char* msg = "CLOSE_RESTAURANT";
    write(fd, msg, strlen(msg) + 1);
    close(fd);
}

void start_manager() {
    signal(SIGINT, SIG_IGN);
    //signal(SIGRTMIN+1, terminate_handler);

    RestaurantState* state = get_state();
    fifo_open_write();

    /* INIT STATE */
    P(SEM_MUTEX_STATE);

    state->restaurantMode = OPEN;
    state->nextDishID = 1;

    for (int i = 0; i < TABLE_COUNT; ++i) {
        Table& t = state->tables[i];

        t.tableID = i;
        t.occupiedSeats = 0;

        if (i < X1)
            t.capacity = 1;
        else if (i < X1 + X2)
            t.capacity = 2;
        else if (i < X1 + X2 + X3)
            t.capacity = 3;
        else
            t.capacity = 4;

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            t.slots[s].pid = -1;
            t.slots[s].size = 0;
            t.slots[s].vipStatus = false;
        }
    }


    V(SEM_MUTEX_STATE);

    while (!terminate_flag) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "\033[35m[%ld] [MANAGER]: TICK\033[0m", time(NULL));
        fifo_log(buffer);

        P(SEM_MUTEX_STATE);

        if (restaurantModeFlag) {
            int speed = state->simulation_speed;

            if ((speed == 0.5 || speed == 1) && restaurantModeFlag == 1) {
                state->simulation_speed *= 2;
            }
            else if ((speed == 1 || speed == 2) && restaurantModeFlag == 2) {
                state->simulation_speed *= 0.5;
            }
            else if (restaurantModeFlag == 4) {
                state->restaurantMode = CLOSED;
            }

            restaurantModeFlag = 0;
        }

        V(SEM_MUTEX_STATE);

        sleep(1);
    }

    fifo_close_write();
}

