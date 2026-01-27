#include "manager.h"

volatile sig_atomic_t manager_cmd = 0;

void handle_manager_signal(int sig) {
    if (sig == SIGUSR1) manager_cmd = 1;      // faster
    else if (sig == SIGUSR2) manager_cmd = 2; // slower
    else if (sig == SIGTERM) manager_cmd = 3; // evacuate
}


void send_close_signal() {
    int fd = open(CLOSE_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;  // brak czytelników jeszcze
    const char* msg = "CLOSE_RESTAURANT";
    write(fd, msg, strlen(msg) + 1);
    close(fd);
}

void start_manager() {
    struct sigaction sa = { 0 };
    sa.sa_handler = handle_manager_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    RestaurantState* state = get_state();
    fifo_open_write();

    /* INIT STATE */
    P(SEM_MUTEX_STATE);

    state->restaurantMode = OPEN;
    state->simulationSpeed = SPEED_NORMAL;
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


    int speed = 1;
    while (!terminate_flag && !evacuate_flag) {
        if (manager_cmd != 0) {
            P(SEM_MUTEX_STATE);

            switch (manager_cmd) {
            case 1: // faster
                if (state->simulationSpeed < SPEED_FAST)
                    state->simulationSpeed++;
                break;

            case 2: // slower
                if (state->simulationSpeed > SPEED_SLOW)
                    state->simulationSpeed--;
                break;

            case 3: // evacuate
                P(SEM_MUTEX_STATE);
                state->restaurantMode = CLOSED;
                evacuate_flag = 1; // jeœli jest globalna w procesie managera
                V(SEM_MUTEX_STATE);

                // opcjonalnie: rozsy³asz sygna³ do wszystkich klientów
                kill(0, SIGTERM); // lub dedykowany sygna³
                break;
            }

            manager_cmd = 0;
            speed = state->simulationSpeed;
            V(SEM_MUTEX_STATE);
        }


        char buffer[128];
        snprintf(buffer, sizeof(buffer), "\033[35m[%ld] [MANAGER %d]: Current simulation speed: %d\033[0m", time(NULL), getpid(), speed);
        fifo_log(buffer);

        sleep(1);
    }

    fifo_close_write();
}

