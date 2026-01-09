#include "client.h"

void start_clients() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    RestaurantState* state = get_state();
    fifo_open_write();

    for (int i = 0; i < 10; i++) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;

        P(SEM_MUTEX_STATE);
        int g_id = state->nextGroupID++;
        V(SEM_MUTEX_STATE);

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "CLIENTS: the %d group arrived", g_id);

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
            write(STDOUT_FILENO, "Ale jazda\n", 10);
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

static int check_close_signal() {
    char buf[32];
    int fd = open(CLOSE_FIFO, O_RDONLY | O_NONBLOCK);
    if (fd == -1) return 0; // FIFO nie istnieje
    int ret = read(fd, buf, sizeof(buf));
    close(fd);
    if (ret > 0 && strcmp(buf, "CLOSE_RESTAURANT") == 0)
        return 1; // sygna³ zamkniêcia
    return 0;
}

