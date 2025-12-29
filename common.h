#pragma once

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
#define N (X1 + 2 * X2 + 3 * X3 + 4 * X4)

// Pojemnoœæ taœmy (liczba talerzyków)
#define P 10

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
	Dish belt[P];

	int soldCount[COLOR_COUNT];
	int soldValue[COLOR_COUNT];
	int remainingCount[COLOR_COUNT];

};

