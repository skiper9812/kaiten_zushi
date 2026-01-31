#include "reports.h"
#include "ipc_manager.h"

// Prints the total production report (Chef)
void printChefReport(RestaurantState* state) {
    printf("\n========== CHEF REPORT ==========\n");
    printf("Products produced:\n");
    
    int totalCount = 0;
    int totalValue = 0;
    
    for (int i = 0; i < COLOR_COUNT; ++i) {
        if (state->producedCount[i] > 0) {
            colors c = colorFromIndex(i);
            printf("  %s: %d pcs - %d PLN\n", 
                colorToString(c), 
                state->producedCount[i], 
                state->producedValue[i]);
            totalCount += state->producedCount[i];
            totalValue += state->producedValue[i];
        }
    }
    
    printf("TOTAL: %d dishes, %d PLN\n", totalCount, totalValue);
    printf("==================================\n\n");
}

// Prints the sales report (Cashier)
void printCashierReport(RestaurantState* state) {
    printf("\n========== CASHIER REPORT ==========\n");
    printf("Products sold:\n");
    
    int totalCount = 0;
    int totalValue = 0;
    
    for (int i = 0; i < COLOR_COUNT; ++i) {
        if (state->soldCount[i] > 0) {
            colors c = colorFromIndex(i);
            printf("  %s: %d pcs - %d PLN\n", 
                colorToString(c), 
                state->soldCount[i], 
                state->soldValue[i]);
            totalCount += state->soldCount[i];
            totalValue += state->soldValue[i];
        }
    }
    
    printf("TOTAL: %d dishes, %d PLN revenue\n", totalCount, state->revenue);
    printf("====================================\n\n");
}

// Prints items remaining on the belt
void printServiceReport(RestaurantState* state) {
    printf("\n========== SERVICE REPORT ==========\n");
    printf("Products remaining on belt:\n");
    
    int remainingByColor[COLOR_COUNT] = {0};
    int remainingValue[COLOR_COUNT] = {0};
    int totalRemaining = 0;
    int totalRemainingValue = 0;
    
    for (int i = 0; i < BELT_SIZE; ++i) {
        Dish& d = state->belt[i];
        if (d.dishID != 0) {
            int colorIdx = colorToIndex(d.color);
            remainingByColor[colorIdx]++;
            remainingValue[colorIdx] += d.price;
            totalRemaining++;
            totalRemainingValue += d.price;
        }
    }
    
    for (int i = 0; i < COLOR_COUNT; ++i) {
        if (remainingByColor[i] > 0) {
            colors c = colorFromIndex(i);
            printf("  %s: %d pcs - %d PLN\n", 
                colorToString(c), 
                remainingByColor[i], 
                remainingValue[i]);
        }
    }
    
    printf("TOTAL remaining: %d dishes, %d PLN value\n", totalRemaining, totalRemainingValue);
    printf("=====================================\n\n");
}

// Prints wasted items (abandoned on tables)
void printWastedReport(RestaurantState* state) {
    printf("\n========== WASTED REPORT ==========\n");
    printf("Premium dishes removed (group finished early):\n");
    
    int totalWasted = 0;
    int totalWastedValue = 0;
    
    for (int i = 0; i < COLOR_COUNT; ++i) {
        if (state->wastedCount[i] > 0) {
            colors c = colorFromIndex(i);
            printf("  %s: %d pcs - %d PLN\n", 
                colorToString(c), 
                state->wastedCount[i], 
                state->wastedValue[i]);
            totalWasted += state->wastedCount[i];
            totalWastedValue += state->wastedValue[i];
        }
    }
    
    if (totalWasted == 0) {
        printf("  (none)\n");
    }
    
    printf("TOTAL wasted: %d dishes, %d PLN value\n", totalWasted, totalWastedValue);
    printf("====================================\n\n");
}

// Orchestrates the printing of all final reports and performs data validation
void printAllReports(RestaurantState* state) {
    printf("\n\n");
    printf("============================================================\n");
    printf("           SIMULATION FINISHED - FINAL REPORTS\n");
    printf("============================================================\n");
    
    printChefReport(state);
    printCashierReport(state);
    printServiceReport(state);
    printWastedReport(state);
    
    // Validation check: Conservation of Mass/Value
    int totalProduced = 0;
    int totalSold = 0;
    int totalRemaining = 0;
    int totalWasted = 0;
    
    for (int i = 0; i < COLOR_COUNT; ++i) {
        totalProduced += state->producedCount[i];
        totalSold += state->soldCount[i];
        totalWasted += state->wastedCount[i];
    }
    
    for (int i = 0; i < BELT_SIZE; ++i) {
        if (state->belt[i].dishID != 0) totalRemaining++;
    }
    
    printf("\n========== VALIDATION ==========\n");
    printf("Produced: %d\n", totalProduced);
    printf("Sold:     %d\n", totalSold);
    printf("Remaining:%d\n", totalRemaining);
    printf("Wasted:   %d\n", totalWasted);
    printf("----------------------------------\n");
    int calculated = totalSold + totalRemaining + totalWasted;
    printf("Sold+Remaining+Wasted = %d\n", calculated);
    if (totalProduced == calculated) {
        printf("+ MATCH: Produced == Sold+Remaining+Wasted\n");
    } else {
        printf("- MISMATCH: Produced (%d) != Sold+Remaining+Wasted (%d)\n", totalProduced, calculated);
        printf("  Difference: %d dishes unaccounted\n", totalProduced - calculated);
    }
    printf("=================================\n\n");
}
