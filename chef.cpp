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
        
#if STRESS_TEST
        // In this test, stop cooking immediately if belt is completely full.
        // Checking slotIdx is not enough (we just filled one), check global count?
        // Actually, if we just successfully put a dish, check if belt is full now.
        // A simple approximation: check if we filled the last available slot?
        // Better: Chef loop will check belt before cooking next time. 
        // But the requirement says "cook until belt is full".
        // Let's rely on the loop check or add a wait here.
#endif

    } else {
        V(SEM_BELT_SLOTS);
#if STRESS_TEST
        // Belt is full (no empty slot found).
        // Requirement: "stop chef process"
        char logBuf[128];
        snprintf(logBuf, sizeof(logBuf), "\033[31m[%ld] [CHEF]: BELT FULL (STRESS TEST) - STOPPING\033[0m", time(NULL));
        fifoLog(logBuf);
        // Wait indefinitely (emulating 'stop')
        while(!terminate_flag && !evacuate_flag) sleep(1);
#endif
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

    // Local counter for premium dishes cooked by this chef process
    int premiumCookedCount = 0;

    while (!terminate_flag && !evacuate_flag) {
#if STRESS_TEST
        // Check if belt is full BEFORE trying to cook (which would block)
        // We can check produced count or semaphore value.
        // Or simply wait for belt drain.
        // Actually, we need to stop cooking once we hit 500 items.
        // Let's count items on belt.
        
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

        if (queueRecvRequest(order)) {
            chefPutDish(state, order.dish, order.groupID);
            premiumCookedCount++;
        } else {
#if PREDEFINED_ZOMBIE_TEST
            // In Zombie Test, only cook normal dishes if we have already cooked 100 premium dishes
            if (premiumCookedCount >= 100) {
                chefPutDish(state, -1, -1);
            }
#else
            chefPutDish(state, -1, -1);
#endif
        }

        P(SEM_MUTEX_STATE);
        speed = state->simulationSpeed;
        V(SEM_MUTEX_STATE);

        long wait = sleepTime(250000, speed);

        SIM_SLEEP(wait);
    }

    fifoCloseWrite();
}
