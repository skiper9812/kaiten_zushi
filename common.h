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
#define BELT_SIZE 10
#define MSG_PREMIUM_TYPE 1
#define MAX_QUEUE 20
#define COLOR_COUNT 6

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

typedef enum { OPEN = 1, SLOW_MODE = 2, FAST_MODE = 3, EVACUATION = 4 } restaurantMode;

struct GroupQueue {
    int groupPid[MAX_QUEUE];
    int count;
};

struct Table {
    int tableID;
    int capacity;
    int occupiedSeats;
    int slotGroupID[2];
};

struct Dish {
    int dishID;
    colors color;
    int price;
    int targetTableID;
};

class Group;

struct RestaurantState {
    int restaurantMode;

    int currentGuestCount;
    int currentVIPCount;

    int nextDishID;
    int beltHead;
    int beltTail;

    Table tables[TABLE_COUNT];
    Dish belt[BELT_SIZE];

    GroupQueue normalQueue;
    GroupQueue vipQueue;

    int soldCount[COLOR_COUNT];
    int soldValue[COLOR_COUNT];
    int revenue;
    int remainingCount[COLOR_COUNT];

    int sig_accelerate;
    int sig_slowdown;
    int sig_evacuate;
};

enum {
    SEM_MUTEX_STATE = 0,
    SEM_MUTEX_QUEUE,
    SEM_BELT_SLOTS,
    SEM_BELT_ITEMS,
    SEM_TABLES,
    SEM_QUEUE_FREE_VIP,
    SEM_QUEUE_FREE_NORMAL,
    SEM_QUEUE_USED_VIP,
    SEM_QUEUE_USED_NORMAL,
    SEM_COUNT
};