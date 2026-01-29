#include "service.h"

static void cleanZombieDishes(RestaurantState* state, int groupID) {
    for (int i = 0; i < BELT_SIZE; ++i) {
        Dish& d = state->belt[i];
        if (d.dishID == 0) continue;
        if (d.targetGroupID == groupID) {
            // Track wasted dish before removing
            int colorIdx = colorToIndex(d.color);
            state->wastedCount[colorIdx]++;
            state->wastedValue[colorIdx] += d.price;

            d.dishID = 0;
            d.targetGroupID = -1;
            V(SEM_BELT_SLOTS);
        }
    }
}

int assignTable(RestaurantState* state, bool vipStatus, int groupSize, int groupID, pid_t pid) {
    for (int i = 0; i < TABLE_COUNT; ++i) {
        P(SEM_MUTEX_STATE);
        Table& t = state->tables[i];

        if (vipStatus && t.capacity == 1) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        int freeSeats = t.capacity - t.occupiedSeats;
        if (freeSeats < groupSize) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        int referenceSize = -1;
        bool compatible = true;

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid == -1) continue;
            if (referenceSize == -1)
                referenceSize = t.slots[s].size;
            else if (t.slots[s].size != referenceSize) {
                compatible = false;
                break;
            }
        }

        if (referenceSize != -1 && referenceSize != groupSize)
            compatible = false;

        if (!compatible) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != -1) continue;

            t.slots[s].pid = pid;
            t.slots[s].size = groupSize;
            t.slots[s].vipStatus = vipStatus;

            t.occupiedSeats += groupSize;
            state->currentGuestCount += groupSize;
            if (vipStatus) state->currentVIPCount++;

            V(SEM_MUTEX_STATE);
            return i;
        }

        V(SEM_MUTEX_STATE);
    }
    return -1;
}

void handleQueueGroup(const ClientRequest& req) {
    char logBuffer[256];
    
    // Blocking queue - queuePush will block until space available
    bool queued = queuePush(req.pid, req.vipStatus);
    
    if (queued) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE]: GROUP QUEUED | pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), req.pid, req.groupID, req.groupSize, req.vipStatus);
        fifoLog(logBuffer);
        return;
    }
    
    // Only reach here if terminated during blocking wait
    snprintf(logBuffer, sizeof(logBuffer),
        "\033[33m[%ld] [SERVICE]: GROUP QUEUE CANCELLED | pid=%d groupID=%d reason=SHUTDOWN\033[0m",
        time(NULL), req.pid, req.groupID);
    fifoLog(logBuffer);
}

void handleAssignGroup(RestaurantState* state, const ClientRequest& req) {
    char logBuffer[256];

    P(SEM_MUTEX_STATE);
    int freeSeats = TOTAL_SEATS - state->currentGuestCount;
    V(SEM_MUTEX_STATE);

    if (freeSeats < req.groupSize) {
        handleQueueGroup(req);
        return;
    }

    int assignedTable = assignTable(state, req.vipStatus, req.groupSize, req.groupID, req.pid);

    if (assignedTable != -1) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE]: TABLE ASSIGNED | table=%d pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), assignedTable, req.pid, req.groupID, req.groupSize, req.vipStatus);
        fifoLog(logBuffer);

        ServiceRequest assigned{};
        assigned.mtype = req.pid;
        assigned.type = REQ_GROUP_ASSIGNED;
        assigned.extraData = assignedTable;

        queueSendRequest(assigned);
        return;
    }

    handleQueueGroup(req);
}

void handleGroupFinished(RestaurantState* state, const ClientRequest& req) {
    pid_t pid = req.pid;
    const int* eatenCount = req.eatenCount;

    P(SEM_MUTEX_STATE);
    P(SEM_MUTEX_BELT);

    for (int i = 0; i < TABLE_COUNT; ++i) {
        Table& t = state->tables[i];

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != pid) continue;

            int groupID = req.groupID;
            int size = t.slots[s].size;
            bool vip = t.slots[s].vipStatus;

            cleanZombieDishes(state, groupID);

            // Count total dishes eaten for logging (already tracked in shared memory)
            int groupDishes = 0;
            int groupRevenue = 0;
            for (int j = 0; j < COLOR_COUNT; ++j) {
                if (eatenCount[j] > 0) {
                    groupDishes += eatenCount[j];
                    groupRevenue += eatenCount[j] * priceForColor(colorFromIndex(j));
                }
            }

            t.slots[s].pid = -1;
            t.slots[s].size = 0;
            t.slots[s].vipStatus = false;

            t.occupiedSeats -= size;
            state->currentGuestCount -= size;
            if (vip) state->currentVIPCount--;

            if (t.occupiedSeats == 0) V(SEM_TABLES);

            char logBuffer[256];
            snprintf(logBuffer, sizeof(logBuffer),
                "\033[38;5;118m[%ld] [SERVICE]: GROUP PAID OFF | groupID=%d pid=%d dishes=%d totalPrice=%d\033[0m",
                time(NULL), groupID, pid, groupDishes, groupRevenue);
            fifoLog(logBuffer);

            V(SEM_MUTEX_BELT);
            V(SEM_MUTEX_STATE);
            return;
        }
    }

    V(SEM_MUTEX_BELT);
    V(SEM_MUTEX_STATE);
}

void startService() {
    fifoOpenWrite();

    clientQid = connectQueue(CLIENT_REQ_QUEUE);
    serviceQid = connectQueue(SERVICE_REQ_QUEUE);

    RestaurantState* state = getState();

    while (!terminate_flag && !evacuate_flag) {
        ClientRequest req{};
        queueRecvRequest(req);

        switch (req.type) {
        case REQ_ASSIGN_GROUP:
            handleAssignGroup(state, req);
            break;
        case REQ_GROUP_FINISHED:
            handleGroupFinished(state, req);
            break;
        default:
            break;
        }
    }

    fifoCloseWrite();
}
