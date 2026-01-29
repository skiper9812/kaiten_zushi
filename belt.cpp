#include "belt.h"

void rotateBelt(RestaurantState* state) {
    P(SEM_MUTEX_BELT);
    
    // Check if belt is empty - don't rotate if nothing on it
    bool empty = true;
    for (int i = 0; i < BELT_SIZE; ++i) {
        if (state->belt[i].dishID != 0) {
            empty = false;
            break;
        }
    }
    
    if (!empty) {
        // Rotate: move all items one position forward
        Dish last = state->belt[BELT_SIZE - 1];
        for (int i = BELT_SIZE - 1; i > 0; --i) {
            state->belt[i] = state->belt[i - 1];
        }
        state->belt[0] = last;
        
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "\033[34m[%ld] [BELT]: ROTATED\033[0m",
            time(NULL));
        //fifoLog(buffer);
    }
    
    V(SEM_MUTEX_BELT);
}

void startBelt() {
    RestaurantState* state = getState();
    fifoOpenWrite();
    
    while (!terminate_flag && !evacuate_flag) {
        SIM_SLEEP(500000);  // 500ms rotation interval
        rotateBelt(state);
    }
    
    fifoCloseWrite();
}
