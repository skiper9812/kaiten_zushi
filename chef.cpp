#include "chef.h"

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

void chef_put_dish(RestaurantState* state, int target) {
    int idx = rand() % 3 + (target == -1 ? 0 : 3);   // 1–3 basic, 4–6 premium

    Dish plate;
    plate.color = colorFromIndex(idx);
    plate.price = priceForColor(plate.color);
    plate.targetTableID = target;

    P(SEM_BELT_SLOTS);
    P(SEM_MUTEX_BELT);

    Dish last = state->belt[BELT_SIZE - 1];

    for (int i = BELT_SIZE - 1; i > 0; --i)
        state->belt[i] = state->belt[i - 1];

    state->belt[0] = last;

    Dish& slot = state->belt[0];

    if (slot.dishID == 0) {
        plate.dishID = state->nextDishID++;
        slot = plate;
        V(SEM_BELT_ITEMS);
    } else
        V(SEM_BELT_SLOTS);

    V(SEM_MUTEX_BELT);

    /*if (target != -1) {
        send_premium_order(target, plate.price);
    }*/

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "\033[38;5;214m[%ld] [CHEF]: DISH %d COOKED | color=%s price=%d targetTableID=%d\033[0m",
        time(NULL), plate.dishID, colorToString(plate.color), plate.price, plate.targetTableID);
    fifo_log(buffer);
}



void start_chef() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    RestaurantState* state = get_state();
    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);

    fifo_open_write();

    while (true) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;

        chef_put_dish(state, -1);

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

