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

// =====================================================
// CONFIGURATION MACROS
// =====================================================
#define FIFO_PATH "/tmp/restauracja_fifo"
#define CLOSE_FIFO "/tmp/close_restaurant_fifo"

// Set to 1 to skip all sleeps during testing
#define SKIP_DELAYS 1

#if SKIP_DELAYS
    #define SIM_SLEEP(us) ((void)0)
#else
    #define SIM_SLEEP(us) usleep(us)
#endif

// Simulation duration in seconds (maps to TP->TK restaurant hours)
#define SIMULATION_DURATION_SECONDS 10

// Set to 1 to enable specific stress test: 5k clients wait, Chef fills belt, then clients enter
#define STRESS_TEST 0

// Set to 1 (No Clean) or 2 (With Clean) to enable ZOMBIE_TEST
// 1 client, 300 premium orders, eats 1, leaves.
#define ZOMBIE_TEST 0
#define PREDEFINED_ZOMBIE_TEST (ZOMBIE_TEST > 0)
#if PREDEFINED_ZOMBIE_TEST
    // Override duration for Zombie Test
#undef SIMULATION_DURATION_SECONDS
#define SIMULATION_DURATION_SECONDS 60
#endif

// Set to 1 to enable specific stress test: 5k clients wait, Chef fills belt, then clients enter
#define CRITICAL_TEST 0

// Set to specific number (>= 0) to generate exact number of groups. 
// Set to -1 for random/infinite generation (until time ends).
// Set to 1 to enable Table Sharing Test (X3/X4 only, Group Size 1-2)
#define TABLE_SHARING_TEST 0

// Set to specific number (>= 0) to generate exact number of groups. 
// Set to -1 for random/infinite generation (until time ends).
#define FIXED_GROUP_COUNT 1000

#define MAX_QUEUE 1000
#define BELT_SIZE 100

// Table counts by size
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

// Derived constants
#define TOTAL_SEATS (X1 + 2 * X2 + 3 * X3 + 4 * X4)
#define TABLE_COUNT (X1 + X2 + X3 + X4)
#define COLOR_COUNT 6
#define MAX_TABLE_SLOTS 4

typedef enum { WHITE = 0, YELLOW, GREEN, RED, BLUE, PURPLE } colors;

static int priceForColor(colors c) {
    static const int prices[COLOR_COUNT] = { 10, 15, 20, 40, 50, 60 };
    return prices[c];
}

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

struct GroupQueue {
    int groupPid[MAX_QUEUE];
    int groupSize[MAX_QUEUE];
    int groupID[MAX_QUEUE];
    int count;
};

struct TableSlot {
    pid_t pid;
    int size;
    bool vipStatus;
};

struct Table {
    int tableID;
    int capacity;
    int occupiedSeats;
    TableSlot slots[MAX_TABLE_SLOTS];
};

struct Dish {
    int dishID;
    colors color;
    int price;
    int targetGroupID;
};

typedef enum {
    SPEED_SLOW = 0,
    SPEED_NORMAL = 1,
    SPEED_FAST = 2
} SimulationSpeed;

class Group;

struct RestaurantState {
    int restaurantMode;
    int simulationSpeed;

    int currentGuestCount;
    int currentVIPCount;
    int totalGroupsCreated; // Tracks total groups successfully forked, for Barrier logic

    int nextDishID;
#if CRITICAL_TEST
    int suicideTriggered; // Flag to ensure only one process triggers the random kill test
#endif

    time_t startTime;
    long long totalPauseNanoseconds;

    Table tables[TABLE_COUNT];
    Dish belt[BELT_SIZE];

    GroupQueue normalQueue;
    GroupQueue vipQueue;

    int producedCount[COLOR_COUNT];
    int producedValue[COLOR_COUNT];
    int remainingCount[COLOR_COUNT];
    int soldCount[COLOR_COUNT];
    int soldValue[COLOR_COUNT];
    int wastedCount[COLOR_COUNT];
    int wastedValue[COLOR_COUNT];
    int revenue;
    
    int sigAccelerate;
    int sigSlowdown;
    int sigEvacuate;
};

enum {
    SEM_MUTEX_STATE = 0,
    SEM_MUTEX_LOGS,

    SEM_MUTEX_BELT,
    SEM_BELT_SLOTS,
    SEM_BELT_ITEMS,

    SEM_TABLES,
    SEM_CLIENT_FREE,
    SEM_CLIENT_ITEMS,

    SEM_SERVICE_FREE,
    SEM_SERVICE_ITEMS,

    SEM_PREMIUM_FREE,
    SEM_PREMIUM_ITEMS,

    SEM_MUTEX_QUEUE,
    SEM_QUEUE_FREE_VIP,
    SEM_QUEUE_FREE_NORMAL,
    SEM_QUEUE_USED_VIP,
    SEM_QUEUE_USED_NORMAL,

    SEM_COUNT
};
