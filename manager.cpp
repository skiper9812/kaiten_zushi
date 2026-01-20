#include "manager.h"

void send_close_signal() {
    int fd = open(CLOSE_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;  // brak czytelników jeszcze
    const char* msg = "CLOSE_RESTAURANT";
    write(fd, msg, strlen(msg) + 1);
    close(fd);
}

void start_manager() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    RestaurantState* state = get_state();
    fifo_open_write();
    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);

    /* INIT STATE */
    P(SEM_MUTEX_STATE);

    state->restaurantMode = OPEN;
    state->beltHead = 0;
    state->beltTail = 0;
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

    while (true) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;


        char buffer[128];
        snprintf(buffer, sizeof(buffer),"\033[35m[%ld] [MANAGER] TICK\033[0m",time(NULL));
        fifo_log(buffer);

        P(SEM_MUTEX_STATE);

        if (state->sig_accelerate) {
            do_accel = 1;
            state->sig_accelerate = 0;
        }

        if (state->sig_slowdown) {
            do_slow = 1;
            state->sig_slowdown = 0;
        }

        if (state->sig_evacuate) {
            do_evacuate = 1;
            state->sig_evacuate = 0;
        }

        V(SEM_MUTEX_STATE);

        if (do_accel) {
            /* przyspieszenie */
        }

        if (do_slow) {
            /* spowolnienie */
        }

        if (do_evacuate) {
            /* natychmiastowe wyjœcie */
        }

        sleep(1);
    }

    fifo_close_write();
}

