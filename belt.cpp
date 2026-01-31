#include "belt.h"

// Rotates the conveyor belt by shifting all items one index
// and wrapping the last item to the first index.
void rotateBelt(RestaurantState* state) {
    P(SEM_MUTEX_BELT);
    
    // Optimization: only rotate if belt is not empty
    bool empty = true;
    for (int i = 0; i < BELT_SIZE; ++i) {
        if (state->belt[i].dishID != 0) {
            empty = false;
            break;
        }
    }
    
    if (!empty) {
        Dish last = state->belt[BELT_SIZE - 1];
        for (int i = BELT_SIZE - 1; i > 0; --i) {
            state->belt[i] = state->belt[i - 1];
        }
        state->belt[0] = last;
        
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
            "\033[34m[%ld] [BELT]: ROTATED\033[0m",
            time(NULL));
    }
    
    V(SEM_MUTEX_BELT);
}

// Main belt process loop
// Automatically logs start/stop via FIFO init/close
void startBelt() {
    RestaurantState* state = getState();
    fifoOpenWrite();
    
    while (!terminate_flag && !evacuate_flag) {
        SIM_SLEEP(500000); // 500ms rotation interval
        rotateBelt(state);
    }
    
    fifoCloseWrite();
}
