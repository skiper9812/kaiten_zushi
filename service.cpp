#include "service.h"

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

void assign_tables(RestaurantState* state) {
    for (int i = 0; i < TABLE_COUNT; i++) {

        P(SEM_MUTEX_STATE);
        Table* t = &state->tables[i];
        int freeSeats = t->capacity - t->occupiedSeats;
        V(SEM_MUTEX_STATE);

        if (freeSeats <= 0)
            continue;

        Group* grp = queue_pop(freeSeats, true);
        if (!grp)
            grp = queue_pop(freeSeats, false);

        if (grp) {
            char buf[128];
            snprintf(buf, sizeof(buf), "SERVICE: picked group %d from queue", grp->groupID);
            fifo_log(buf);
        }

        if (!grp)
            continue;

        P(SEM_MUTEX_STATE);

        freeSeats = t->capacity - t->occupiedSeats;

        if (freeSeats < grp->groupSize) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        if (t->occupiedSeats == 0) {
            P(SEM_TABLES);
            t->group[0] = grp;
            fifo_log("SERVICE: assigned group to empty slot 0");
        }
        else if (t->group[0] &&
            !t->group[1] &&
            t->group[0]->groupSize == grp->groupSize) {
            t->group[1] = grp;
            fifo_log("SERVICE: assigned group to empty slot 1");
        }
        else {
            V(SEM_MUTEX_STATE);
            fifo_log("SERVICE: could not assign group due to size mismatch or full table");
            continue;
        }

        t->occupiedSeats += grp->groupSize;
        state->currentGuestCount += grp->groupSize;
        if (grp->isVip) state->currentVIPCount++;

        V(SEM_MUTEX_STATE);

        if (grp->isVip) V(SEM_QUEUE_USED_VIP);
        else V(SEM_QUEUE_USED_NORMAL);
    }
}

void free_table(RestaurantState* state, Group* grp) {

    for (int i = 0; i < TABLE_COUNT; i++) {
        Table* t = &state->tables[i];
        if (t->group[0] == grp) {
            t->group[0] = t->group[1];
            t->group[1] = NULL;
        }
        else if (t->group[1] == grp) {
            t->group[1] = NULL;
        }

        if (t->group[0] == NULL && t->group[1] == NULL)
            t->occupiedSeats = 0;
        else
            t->occupiedSeats = 0 + (t->group[0] ? t->group[0]->groupSize : 0)
            + (t->group[1] ? t->group[1]->groupSize : 0);
    }

    state->currentGuestCount -= grp->groupSize;
    if (grp->isVip) state->currentVIPCount--;

    // aktualizacja statystyk sprzeda¿y
    for (int c = 0; c < COLOR_COUNT; c++)
        state->soldCount[c] += grp->eatenCount[c];

    V(SEM_TABLES);

    // logowanie
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "SERVICE: Group %d left the table | freed seats=%d",
        grp->groupID, grp->groupSize);
    fifo_log(buffer);
}

void start_service() {
    /* REGISTER SIGNALS */
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    RestaurantState* state = get_state();
    fifo_open_write();

    

    //for (int i = 0; i < 10; ++i) {
    while(true){
        fifo_log("SERVICE: start iteration of service loop");

        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;

        assign_tables(state);

        P(SEM_MUTEX_STATE);

        for (int i = 0; i < TABLE_COUNT; i++) {
            Table* t = &state->tables[i];

            for (int g = 0; g < 2; g++) {
                Group* grp = t->group[g];
                if (grp && grp->dishesToEat == 0) {
                    free_table(state, grp);
                }
            }
        }
        V(SEM_MUTEX_STATE);

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

