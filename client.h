#pragma once
#include "ipc_manager.h"
#include "common.h"
#include <pthread.h>

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
    int ordersLeft;
    int eatenCount[COLOR_COUNT];
    pthread_mutex_t mutex;

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
        ordersLeft = rand() % dishesToEat;
        memset(eatenCount, 0, sizeof(eatenCount));
        pthread_mutex_init(&mutex, nullptr);
    }

    ~Group() {
        pthread_mutex_destroy(&mutex);
    }

    // ===== gettery =====
    int  getGroupID() const { return groupID; }
    int  getGroupSize() const { return groupSize; }
    int  getAdultCount() const { return adultCount; }
    int  getChildCount() const { return childCount; }
    bool getVipStatus() const { return vipStatus; }
    int  getDishesToEat() const { return dishesToEat; }
    int  getTableIndex() const { return tableIndex; }
    int  getOrdersLeft() { pthread_mutex_lock(&mutex); int o = ordersLeft; pthread_mutex_unlock(&mutex); return o; }
    const int* getEatenCount() const { return eatenCount; }

    // ===== settery =====
    void setTableIndex(int idx) { tableIndex = idx; }

    // ===== mutatory logiczne =====
    bool orderPremiumDish() {
        bool orderPremium = (rand() % 100) < 20;
        pthread_mutex_lock(&mutex);
        if (orderPremium && ordersLeft > 0) {
            ordersLeft--;
            pthread_mutex_unlock(&mutex);
            return true;
        }
        pthread_mutex_unlock(&mutex);
        return false;
    }

    bool consumeOneDish(colors c) {
        pthread_mutex_lock(&mutex);
        if (dishesToEat <= 0) {
            pthread_mutex_unlock(&mutex);
            return false;
        }

        dishesToEat--;
        int idx = colorToIndex(c);
        if (idx >= 0 && idx < COLOR_COUNT)
            eatenCount[idx]++;

        pthread_mutex_unlock(&mutex);
        return true;
    }

    bool isFinished() {
        pthread_mutex_lock(&mutex);
        bool done = (dishesToEat <= 0);
        pthread_mutex_unlock(&mutex);
        return done;
    }
};

struct PersonCtx {
    Group* group;
    int personID;
};

// =====================================================
// API procesu clients
// =====================================================

void start_clients();
void group_loop(Group& g);
