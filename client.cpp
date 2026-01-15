#include "client.h"

Group* generate_group(RestaurantState* state) {
    Group* grp = new Group();

    // unikalny ID w sekcji krytycznej
    P(SEM_MUTEX_STATE);
    grp->groupID = state->nextGroupID++;
    V(SEM_MUTEX_STATE);

    grp->groupSize = rand() % 4 + 1;
    grp->isVip = (rand() % 100) < 2;
    grp->childCount = grp->isVip ? 0 : rand() % grp->groupSize;
    grp->adultCount = grp->isVip ? grp->groupSize : grp->groupSize - grp->childCount;
    grp->dishesToEat = rand() % 8 + 3;

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "CLIENTS: GROUP %d arrived | size=%d adults=%d children=%d VIP=%d dishes=%d",
        grp->groupID, grp->groupSize, grp->adultCount, grp->childCount, grp->isVip, grp->dishesToEat);
    fifo_log(buffer);

    return grp;
}

void consume_dish(RestaurantState* state, Group* grp, int* running_bill) {
    Dish plate;

    P(SEM_BELT_ITEMS);

    P(SEM_MUTEX_STATE);
    plate = state->belt[state->beltHead];
    state->beltHead = (state->beltHead + 1) % BELT_SIZE;
    V(SEM_MUTEX_STATE);

    V(SEM_BELT_SLOTS);

    if (grp->dishesToEat > 0) {
        grp->dishesToEat--;
        grp->eatenCount[plate.color]++;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "CLIENT: Group %d consumed dish %d | price=%d remaining dishes=%d",
        grp->groupID, plate.dishID, plate.price, grp->dishesToEat);
    fifo_log(buffer);
}

void start_clients() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    RestaurantState* state = get_state();
    Group* grp;
    fifo_open_write();

    for (int i = 0; i < 10; i++) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;

        grp = generate_group(state);
        queue_push(grp);

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

