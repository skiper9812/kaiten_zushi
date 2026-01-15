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

// Godziny otwarcia restauracji
#define TP 12  // godzina otwarcia
#define TK 20  // godzina zamkniêcia

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

#define MAX_QUEUE 40
#define COLOR_COUNT 6

typedef enum {
	WHITE = 0,
	YELLOW,
	GREEN,
	RED,
	BLUE,
	PURPLE
} colors;

static int priceForColor(colors c) {
	static const int prices[COLOR_COUNT] = {
		10, // WHITE
		15, // YELLOW
		20, // GREEN
		40, // RED
		50, // BLUE
		60  // PURPLE
	};
	return prices[c];
}

static const char* colorToString(colors c) {
	static const char* names[COLOR_COUNT] = {
		"WHITE", "YELLOW", "GREEN", "RED", "BLUE", "PURPLE"
	};
	return names[c];
}

static colors colorFromIndex(int idx) {
	static const colors map[COLOR_COUNT] = {
		WHITE, YELLOW, GREEN, RED, BLUE, PURPLE
	};
	return map[idx];
}

typedef enum {
	OPEN = 1,
	SLOW_MODE = 2,
	FAST_MODE = 3,
	EVACUATION = 4
} restaurantMode;

struct Group {
	int groupID;
	int groupSize;
	int adultCount;
	int childCount;
	bool isVip;
	int dishesToEat;
	int eatenCount[COLOR_COUNT];
};

struct GroupQueue {
	Group* queue[MAX_QUEUE / 2];
	int count;
};

struct Table {
	int tableID;
	int capacity;
	int occupiedSeats;
	Group* group[2];
};

struct Dish {
	int dishID;
	colors color;
	int price;
	int targetTableID;
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

	Table tables[TABLE_COUNT];
	Dish belt[BELT_SIZE];

	GroupQueue normal_queue;
	GroupQueue vip_queue;

	int soldCount[COLOR_COUNT];
	int soldValue[COLOR_COUNT];
	int revenue;
	int remainingCount[COLOR_COUNT];

	int sig_accelerate;   // SIGUSR1
	int sig_slowdown;     // SIGUSR2
	int sig_evacuate;     // SIGTERM

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

struct PremiumMsg {
	long mtype;
	int table_id;
	int dish_price;    // 40 / 50 / 60
};



