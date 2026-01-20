#pragma once
#include "ipc_manager.h"
#include "common.h"

// =====================================================
// Group – czysty obiekt danych
// =====================================================

class Group {
private:
    int groupID;
    int groupSize;
    int adultCount;
    int childCount;
    bool vipStatus;
    int dishesToEat;
    int tableIndex;
    int eatenCount[COLOR_COUNT];

public:
    static int nextGroupID;

    // ===== tworzenie grupy =====
    Group() : tableIndex(-1) {
        groupID = nextGroupID++;
        groupSize = rand() % 4 + 1;
        vipStatus = (rand() % 100) < 2;

        if (vipStatus) {
            childCount = 0;
            adultCount = groupSize;
        }
        else {
            childCount = rand() % groupSize;
            adultCount = groupSize - childCount;
        }

        dishesToEat = rand() % 8 + 3;
        memset(eatenCount, 0, sizeof(eatenCount));
    }

    // ===== gettery =====
    int  getGroupID() const { return groupID; }
    int  getGroupSize() const { return groupSize; }
    int  getAdultCount() const { return adultCount; }
    int  getChildCount() const { return childCount; }
    bool getVipStatus() const { return vipStatus; }
    int  getDishesToEat() const { return dishesToEat; }
    int  getTableIndex() const { return tableIndex; }
    const int* getEatenCount() const { return eatenCount; }

    // ===== settery =====
    void setTableIndex(int idx) { tableIndex = idx; }

    // ===== mutatory logiczne =====
    void consumeOneDish(colors c) {
        int colorIndex = colorToIndex(c);
        if (dishesToEat <= 0) return;
        dishesToEat--;
        if (colorIndex >= 0 && colorIndex < COLOR_COUNT)
            eatenCount[colorIndex]++;
    }

    bool isFinished() const {
        return dishesToEat <= 0;
    }
};

// =====================================================
// API procesu clients
// =====================================================

void start_clients();
void group_loop(Group& g);
