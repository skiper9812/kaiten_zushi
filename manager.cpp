#include "manager.h"

volatile sig_atomic_t managerCmd = 0;

// Handles signals sent to the Manager process (SIGUSR1, SIGUSR2, SIGTERM)
void handleManagerSignal(int sig) {
    if (sig == SIGUSR1) managerCmd = 1;
    else if (sig == SIGUSR2) managerCmd = 2;
    else if (sig == SIGTERM) managerCmd = 3;
}

// Main Manager process loop
// Controls simulation speed and handling emergency closure
void startManager() {
    struct sigaction sa = { 0 };
    sa.sa_handler = handleManagerSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    RestaurantState* state = getState();
    fifoOpenWrite();

    // Initialize Global State
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

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "\033[35m[%ld] [MANAGER]: RESTAURANT OPENED, SPEED=%d | pid=%d\033[0m", time(NULL), state->simulationSpeed, getpid());
    fifoLog(buffer);

    while (!terminate_flag && !evacuate_flag) {
        pause(); // Wait for control signal

        if (managerCmd != 0) {
            P(SEM_MUTEX_STATE);
            int oldSpeed = state->simulationSpeed;

            switch (managerCmd) {
            case 1:
                if (state->simulationSpeed < SPEED_FAST)
                    state->simulationSpeed++;
                break;

            case 2:
                if (state->simulationSpeed > SPEED_SLOW)
                    state->simulationSpeed--;
                break;

            case 3:
                state->restaurantMode = CLOSED;
                evacuate_flag = 1;
                kill(0, SIGTERM); 
                break;
            }

            int newSpeed = state->simulationSpeed;
            managerCmd = 0;
            V(SEM_MUTEX_STATE);

            if (oldSpeed != newSpeed) {
                snprintf(buffer, sizeof(buffer), "\033[35m[%ld] [MANAGER]: SPEED CHANGED %d -> %d | pid=%d\033[0m",
                    time(NULL), oldSpeed, newSpeed, getpid());
                fifoLog(buffer);
            }
        }
    }

    fifoCloseWrite();
}
