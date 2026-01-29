#pragma once
#include "common.h"

// Print final reports after simulation ends
void printChefReport(RestaurantState* state);
void printCashierReport(RestaurantState* state);
void printServiceReport(RestaurantState* state);
void printWastedReport(RestaurantState* state);
void printAllReports(RestaurantState* state);

