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

#define FIFO_PATH "/tmp/restauracja_fifo"
#define CLOSE_FIFO "/tmp/close_restaurant_fifo"

//Godziny otwarcia i zamkniêcia
#define TP 12
#define TK 20

// Liczba stolików wed³ug rozmiaru
#define X1 4
#define X2 4
#define X3 4
#define X4 4

//Reszta sta³ych
#define TOTAL_SEATS (X1 + 2 * X2 + 3 * X3 + 4 * X4)
#define TABLE_COUNT (X1 + X2 + X3 + X4)
#define MAX_DISHES_ON_BELT 10
#define BELT_SIZE TABLE_COUNT
#define MSG_PREMIUM_TYPE 1
#define MAX_QUEUE 20
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

    int nextDishID;

    Table tables[TABLE_COUNT];
    Dish belt[BELT_SIZE];

    GroupQueue normalQueue;
    GroupQueue vipQueue;

    int remainingCount[COLOR_COUNT];
    int soldCount[COLOR_COUNT];
    int soldValue[COLOR_COUNT];
    int revenue;
    
    int sig_accelerate;
    int sig_slowdown;
    int sig_evacuate;
};

enum {
    // Mutexy ogólne
    SEM_MUTEX_STATE = 0,
    SEM_MUTEX_LOGS,

    // Pas transmisyjny (belt)
    SEM_MUTEX_BELT,
    SEM_BELT_SLOTS,
    SEM_BELT_ITEMS,

    // Sto³y i klienci
    SEM_TABLES,
    SEM_CLIENT_FREE,
    SEM_CLIENT_ITEMS,

    // Us³ugi
    SEM_SERVICE_FREE,
    SEM_SERVICE_ITEMS,

    // Premium
    SEM_PREMIUM_FREE,
    SEM_PREMIUM_ITEMS,

    // Kolejka do lokalu
    SEM_MUTEX_QUEUE,
    SEM_QUEUE_FREE_VIP,
    SEM_QUEUE_FREE_NORMAL,
    SEM_QUEUE_USED_VIP,
    SEM_QUEUE_USED_NORMAL,

    SEM_COUNT
};