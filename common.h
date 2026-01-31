#pragma once
#include "error_handler.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "error_handler.h"

// ============================================================================
// CONFIGURATION & CONSTANTS
// ============================================================================

#define FIFO_PATH "/tmp/restauracja_fifo"
#define CLOSE_FIFO "/tmp/close_restaurant_fifo"

// Enable to skip delays for faster testing
#define SKIP_DELAYS 1

#if SKIP_DELAYS
    #define SIM_SLEEP(us) ((void)0)
#else
    #define SIM_SLEEP(us) usleep(us)
#endif

// Base simulation duration in seconds (TP -> TK)
#define SIMULATION_DURATION_SECONDS 10

// Test flags
#define STRESS_TEST 0
#define ZOMBIE_TEST 0
#define PREDEFINED_ZOMBIE_TEST (ZOMBIE_TEST > 0)
#if PREDEFINED_ZOMBIE_TEST
#undef SIMULATION_DURATION_SECONDS
#define SIMULATION_DURATION_SECONDS 60
#endif

#define CRITICAL_TEST 0
#define TABLE_SHARING_TEST 0

// Group generation limit (-1 for infinite)
#define FIXED_GROUP_COUNT 1000

#define MAX_QUEUE 1000
#define BELT_SIZE 100

// Table configuration based on test mode
#if TABLE_SHARING_TEST == 1
#define X1 0
#define X2 0
#define X3 10
#define X4 10
#elif TABLE_SHARING_TEST == 2
#define X1 0
#define X2 0
#define X3 1
#define X4 0
#else
#define X1 10
#define X2 10
#define X3 10
#define X4 10
#endif

#define TOTAL_SEATS (X1 + 2 * X2 + 3 * X3 + 4 * X4)
#define TABLE_COUNT (X1 + X2 + X3 + X4)
#define COLOR_COUNT 6
#define MAX_TABLE_SLOTS 4

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum { WHITE = 0, YELLOW, GREEN, RED, BLUE, PURPLE } colors;

// Helper to get price for a specific dish color
static int priceForColor(colors c) {
    static const int prices[COLOR_COUNT] = { 10, 15, 20, 40, 50, 60 };
    return prices[c];
}

// Helper to stringify color enum
static const char* colorToString(colors c) {
    static const char* names[COLOR_COUNT] = { "WHITE","YELLOW","GREEN","RED","BLUE","PURPLE" };
    return names[c];
}

static colors colorFromIndex(int idx) {
    static const colors map[COLOR_COUNT] = { WHITE, YELLOW, GREEN, RED, BLUE, PURPLE };
    return map[idx];
}

static int colorToIndex(colors c) {
    switch (c) {
    case WHITE: return 0;
    case YELLOW: return 1;
    case GREEN: return 2;
    case RED: return 3;
    case BLUE: return 4;
    case PURPLE: return 5;
    }
    return -1;
}

typedef enum { OPEN = 1, SLOW_MODE = 2, FAST_MODE = 3, CLOSED = 4 } restaurantMode;

// Queue structure for Groups waiting for a table
struct GroupQueue {
    int groupPid[MAX_QUEUE];
    int groupSize[MAX_QUEUE];
    int groupID[MAX_QUEUE];
    int count;
};

// Represents a occupied slot at a table
struct TableSlot {
    pid_t pid;
    int size;
    bool vipStatus;
};

// Represents a dining table
struct Table {
    int tableID;
    int capacity;
    int occupiedSeats;
    TableSlot slots[MAX_TABLE_SLOTS];
};

// Represents a dish on the conveyor belt
struct Dish {
    int dishID;
    colors color;
    int price;
    int targetGroupID; // -1 if available for anyone
};

typedef enum {
    SPEED_SLOW = 0,
    SPEED_NORMAL = 1,
    SPEED_FAST = 2
} SimulationSpeed;

class Group;

// Shared Memory State Structure
// Holds the entire state of the restaurant accessible by all processes
struct RestaurantState {
    int restaurantMode;
    int simulationSpeed;

    int currentGuestCount;
    int currentVIPCount;
    int totalGroupsCreated; // For barrier synchronization

    int nextDishID;
#if CRITICAL_TEST
    int suicideTriggered;
#endif

    time_t startTime;
    long long totalPauseNanoseconds;

    Table tables[TABLE_COUNT];
    Dish belt[BELT_SIZE];

    GroupQueue normalQueue;
    GroupQueue vipQueue;

    // Statistics
    int producedCount[COLOR_COUNT];
    int producedValue[COLOR_COUNT];
    int remainingCount[COLOR_COUNT];
    int soldCount[COLOR_COUNT];
    int soldValue[COLOR_COUNT];
    int wastedCount[COLOR_COUNT];
    int wastedValue[COLOR_COUNT];
    int revenue;
};

// Semaphore Indices
enum {
    SEM_MUTEX_STATE = 0,    // Protects shared memory state
    SEM_MUTEX_LOGS,         // Protects FIFO logging

    SEM_MUTEX_BELT,         // Protects belt array access
    SEM_BELT_SLOTS,         // Counts empty slots on belt (Producer throttling)
    SEM_BELT_ITEMS,         // Counts items on belt (Consumer indication)

    SEM_TABLES,             // Counts free tables (currently used for general occupancy)
    SEM_CLIENT_FREE,        // Throttles Client Process requests
    SEM_CLIENT_ITEMS,       // Indicates requests for Client Process

    SEM_SERVICE_FREE,       // Throttles Service Process requests
    SEM_SERVICE_ITEMS,      // Indicates requests for Service Process

    SEM_PREMIUM_FREE,       // Throttles Premium Chef requests
    SEM_PREMIUM_ITEMS,      // Indicates requests for Chef Process

    SEM_MUTEX_QUEUE,        // Protects waiting queues
    SEM_QUEUE_FREE_VIP,     // Throttles VIP admission (Backpressure)
    SEM_QUEUE_FREE_NORMAL,  // Throttles Normal admission (Backpressure)
    SEM_QUEUE_USED_VIP,     // (Legacy) could indicate used VIP slots
    SEM_QUEUE_USED_NORMAL,  // (Legacy) could indicate used Normal slots

    SEM_COUNT
};
