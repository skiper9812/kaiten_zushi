#include "chef.h"

// Places a dish on the conveyor belt
// dish=-1 for random dish, or specific ID for premium orders
void chefPutDish(RestaurantState* state, int dish, int target) {
    int idx = dish > 2 ? dish : rand() % 3;

    Dish plate;
    plate.color = colorFromIndex(idx);
    plate.price = priceForColor(plate.color);
    plate.targetGroupID = target;

    // Wait for free slot on belt
    P(SEM_BELT_SLOTS);
    P(SEM_MUTEX_BELT);

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

        V(SEM_BELT_ITEMS); // Notify consumers

        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "\033[38;5;214m[%ld] [CHEF]: DISH %d COOKED | slot=%d color=%s price=%d targetGroupID=%d\033[0m",
            time(NULL), plate.dishID, slotIdx, colorToString(plate.color), plate.price, plate.targetGroupID);
        fifoLog(buffer);

    } else {
        // Should not happen if semaphore logic is correct
        V(SEM_BELT_SLOTS);
#if STRESS_TEST
        char logBuf[128];
        snprintf(logBuf, sizeof(logBuf), "\033[31m[%ld] [CHEF]: BELT FULL (STRESS TEST) - STOPPING\033[0m", time(NULL));
        fifoLog(logBuf);
        while(!terminate_flag && !evacuate_flag) sleep(1);
#endif
    }

    V(SEM_MUTEX_BELT);
}

// Determines sleep time multiplier based on simulation speed
static inline int speedMultiplier(int speed) {
    switch (speed) {
    case SPEED_FAST:   return 1;
    case SPEED_NORMAL: return 2;
    case SPEED_SLOW:   return 4;
    default:           return 2;
    }
}

// Calculates random cooking time with variance
long sleepTime(int baseUs, int speed) {
    int mul = speedMultiplier(speed);
    int minUs = 1000000 * mul / 2;
    int maxUs = 2000000 * mul / 2;
    return baseUs + minUs + (rand() % (maxUs - minUs + 1));
}

// Main Chef process loop
void startChef() {
    RestaurantState* state = getState();
    clientQid = connectQueue(CLIENT_REQ_QUEUE);
    serviceQid = connectQueue(SERVICE_REQ_QUEUE);
    premiumQid = connectQueue(PREMIUM_REQ_QUEUE);

    fifoOpenWrite();
    int speed = 1;

    int premiumCookedCount = 0;

    while (!terminate_flag && !evacuate_flag) {
#if STRESS_TEST
        // In Stress Test, stop if belt is full
        P(SEM_MUTEX_BELT);
        int items = 0;
        for(int i=0; i<BELT_SIZE; ++i) if(state->belt[i].dishID != 0) items++;
        V(SEM_MUTEX_BELT);
        
        if (items >= BELT_SIZE) {
             char logBuf[128];
             snprintf(logBuf, sizeof(logBuf), "\033[31m[%ld] [CHEF]: BELT FULL (STRESS TEST) - STOPPING\033[0m", time(NULL));
             fifoLog(logBuf);
             while(!terminate_flag && !evacuate_flag) sleep(1);
             break;
        }
#endif

        PremiumRequest order;

        // Check for premium orders first (Highest priority)
        if (queueRecvRequest(order)) {
            chefPutDish(state, order.dish, order.groupID);
            premiumCookedCount++;
        } else {
            // Cook normal dish
#if PREDEFINED_ZOMBIE_TEST
            if (premiumCookedCount >= 100) {
                chefPutDish(state, -1, -1);
            }
#else
            chefPutDish(state, -1, -1);
#endif
        }

        // Adjust speed dynamically
        P(SEM_MUTEX_STATE);
        speed = state->simulationSpeed;
        V(SEM_MUTEX_STATE);

        long wait = sleepTime(250000, speed);

        SIM_SLEEP(wait);
    }

    fifoCloseWrite();
}
