#pragma once
#include "error_handler.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define FIFO_PATH "/tmp/restauracja_fifo"
#define CLOSE_FIFO "/tmp/close_restaurant_fifo"

// Godziny otwarcia restauracji
#define TP 12  // godzina otwarcia
#define TK 20  // godzina zamkniêcia
#define COLOR_COUNT 6

// Liczba stolików wed³ug rozmiaru
#define X1 4 // 1-osobowe
#define X2 4 // 2-osobowe
#define X3 4 // 3-osobowe
#define X4 4 // 4-osobowe
#define TABLE_COUNT (X1 + X2 + X3 + X4)

// Maksymalna liczba goœci w restauracji
#define TOTAL_SEATS (X1 + 2 * X2 + 3 * X3 + 4 * X4)

// Pojemnoœæ taœmy (liczba talerzyków)
#define BELT_SIZE 10

//Komunikat zamówienia dania premium
#define MSG_PREMIUM_TYPE 1

typedef enum {
	OPEN=1,
	SLOW_MODE=2,
	FAST_MODE=3,
	EVACUATION=4
} restaurantMode;

struct Table {
	int tableID;
	int capacity;
	int isOccupied;
	int groupID;
	int groupSize;
};

struct Dish {
	int dishID;
	int color;
	int price;
	int targetTableID;   // -1 lub tableID
};

struct RestaurantState {
	int currentGuestCount;
	int currentVIPCount;

	int nextGuestID;
	int nextGroupID;
	int nextDishID;

	int restaurantMode;

	int beltHead;
	int beltTail;
	int currentDishCount;

	Table tables[TABLE_COUNT];
	Dish belt[BELT_SIZE];

	int soldCount[COLOR_COUNT];
	int soldValue[COLOR_COUNT];
	int remainingCount[COLOR_COUNT];

	int sig_accelerate;   // SIGUSR1
	int sig_slowdown;     // SIGUSR2
	int sig_evacuate;     // SIGTERM

};

enum {
	SEM_MUTEX_STATE = 0,
	SEM_BELT_SLOTS,
	SEM_BELT_ITEMS,
	SEM_TABLES,
	SEM_COUNT
};

struct PremiumMsg {
	long mtype;
	int table_id;
	int dish_price;    // 40 / 50 / 60
};



