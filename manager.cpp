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
    req_qid = connect_queue(REQ_QUEUE_PROJ);
    resp_qid = connect_queue(RESP_QUEUE_PROJ);

    /* INIT STATE */
    P(SEM_MUTEX_STATE);

    state->restaurantMode = OPEN;
    state->beltHead = 0;
    state->beltTail = 0;
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
    }

    V(SEM_MUTEX_STATE);

    for (int i = 0; i < 10; i++) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;


        //write(STDOUT_FILENO, buffer, strlen(buffer));

        fifo_log("MANAGER: tick");

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

