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

void chef_put_dish(RestaurantState* state, int dish, int target) {
    int idx = dish > 2 ? dish : rand() % 3;

    Dish plate;
    plate.color = colorFromIndex(idx);
    plate.price = priceForColor(plate.color);
    plate.targetGroupID = target;

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

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "\033[38;5;214m[%ld] [CHEF]: DISH %d COOKED | color=%s price=%d targetTableID=%d\033[0m",
        time(NULL), plate.dishID, colorToString(plate.color), plate.price, plate.targetGroupID);
    fifo_log(buffer);
}

double sleep_time(double base, double speed) {
    double min = base * speed;
    double max = base / speed;

    if (min > max) {
        double temp = min;
        min = max;
        max = temp;
    }

    return min + (rand() / (double)RAND_MAX) * (max - min);
}

void start_chef() {
    signal(SIGINT, SIG_IGN);
    //signal(SIGRTMIN+1, terminate_handler);

    RestaurantState* state = get_state();
    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);
    premium_qid = connect_queue(PREMIUM_REQ_QUEUE);

    fifo_open_write();
    double speed;

    while (!terminate_flag) {
        int do_accel = 0;
        int do_slow = 0;
        int do_evacuate = 0;

        PremiumRequest order;

        if (queue_recv_request(order)) {
            chef_put_dish(state, order.dish, order.groupID);
        }
        else {
            chef_put_dish(state, -1, -1);
        }

        P(SEM_MUTEX_STATE);
        speed = state->simulation_speed;
        V(SEM_MUTEX_STATE);

        double wait = sleep_time(1, speed) * 1000000;
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "\033[38;5;214m[%ld] [CHEF]: WAIT %d\033[0m",
            time(NULL), wait);
        fifo_log(buffer);

        //usleep(wait);
    }

    fifo_close_write();
}

