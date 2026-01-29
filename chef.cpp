#include "chef.h"

void chefPutDish(RestaurantState* state, int dish, int target) {
    int idx = dish > 2 ? dish : rand() % 3;

    Dish plate;
    plate.color = colorFromIndex(idx);
    plate.price = priceForColor(plate.color);
    plate.targetGroupID = target;

    P(SEM_BELT_SLOTS);
    P(SEM_MUTEX_BELT);

    // Find first empty slot on belt (no rotation here - belt process handles that)
    int slotIdx = -1;
    for (int i = 0; i < BELT_SIZE; ++i) {
        if (state->belt[i].dishID == 0) {
            slotIdx = i;
            break;
        }
    }

    if (slotIdx != -1) {
        plate.dishID = state->nextDishID++;
        state->belt[slotIdx] = plate;

        int colorIdx = colorToIndex(plate.color);
        state->producedCount[colorIdx]++;
        state->producedValue[colorIdx] += plate.price;

        V(SEM_BELT_ITEMS);

        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "\033[38;5;214m[%ld] [CHEF]: DISH %d COOKED | slot=%d color=%s price=%d targetGroupID=%d\033[0m",
            time(NULL), plate.dishID, slotIdx, colorToString(plate.color), plate.price, plate.targetGroupID);
        fifoLog(buffer);
    } else {
        V(SEM_BELT_SLOTS);
    }

    V(SEM_MUTEX_BELT);
}

static inline int speedMultiplier(int speed) {
    switch (speed) {
    case SPEED_FAST:   return 1;
    case SPEED_NORMAL: return 2;
    case SPEED_SLOW:   return 4;
    default:           return 2;
    }
}

long sleepTime(int baseUs, int speed) {
    int mul = speedMultiplier(speed);
    int minUs = 1000000 * mul / 2;
    int maxUs = 2000000 * mul / 2;
    return baseUs + minUs + (rand() % (maxUs - minUs + 1));
}

void startChef() {
    RestaurantState* state = getState();
    clientQid = connectQueue(CLIENT_REQ_QUEUE);
    serviceQid = connectQueue(SERVICE_REQ_QUEUE);
    premiumQid = connectQueue(PREMIUM_REQ_QUEUE);

    fifoOpenWrite();
    int speed = 1;

    while (!terminate_flag && !evacuate_flag) {
        PremiumRequest order;

        if (queueRecvRequest(order)) {
            chefPutDish(state, order.dish, order.groupID);
        } else {
            chefPutDish(state, -1, -1);
        }

        P(SEM_MUTEX_STATE);
        speed = state->simulationSpeed;
        V(SEM_MUTEX_STATE);

        long wait = sleepTime(250000, speed);

        SIM_SLEEP(wait);
    }

    fifoCloseWrite();
}
