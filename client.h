#pragma once
#include "ipc_manager.h"
#include "common.h"
#include <pthread.h>

// =====================================================
// Group - pure data object
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

    int  getGroupID() const { return groupID; }
    int  getGroupSize() const { return groupSize; }
    int  getAdultCount() const { return adultCount; }
    int  getChildCount() const { return childCount; }
    bool getVipStatus() const { return vipStatus; }
    
    int getDishesToEat() {
        pthread_mutex_lock(&mutex);
        int v = dishesToEat;
        pthread_mutex_unlock(&mutex);
        return v;
    }

    int getTableIndex() {
        pthread_mutex_lock(&mutex);
        int v = tableIndex;
        pthread_mutex_unlock(&mutex);
        return v;
    }

    int getOrdersLeft() { 
        pthread_mutex_lock(&mutex); 
        int o = ordersLeft; 
        pthread_mutex_unlock(&mutex); 
        return o; 
    }

    void getEatenCount(int out[COLOR_COUNT]) {
        pthread_mutex_lock(&mutex);
        memcpy(out, eatenCount, sizeof(eatenCount));
        pthread_mutex_unlock(&mutex);
    }

    void setTableIndex(int idx) {
        pthread_mutex_lock(&mutex);
        tableIndex = idx;
        pthread_mutex_unlock(&mutex);
    }

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
// Client process API
// =====================================================

void startClients();
void groupLoop(Group& g);
