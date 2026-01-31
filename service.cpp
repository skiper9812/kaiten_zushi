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

// Global static for file to track occupancy in zombie test mode
static int zombieTestOccupancy = 0;
static bool zombieGroupFinished = false;

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

// Helper to remove item from queue at index
void removeQueueItem(GroupQueue& q, int index) {
    if (index < 0 || index >= q.count) return;
    for (int i = index; i < q.count - 1; ++i) {
        q.groupPid[i] = q.groupPid[i + 1];
        q.groupSize[i] = q.groupSize[i + 1];
        q.groupID[i] = q.groupID[i + 1];
    }
    q.count--;
}

bool tryAssignFromQueue(
    RestaurantState* state,
    GroupQueue& queue,
    bool isVip,
    int semUsed,
    int semFree,
    bool allowZombieBlocking
) {
    int allocatedTable = -1;
    int pid = -1, size = 0, gid = -1;

    P(SEM_MUTEX_QUEUE);
    for (int i = 0; i < queue.count; ++i) {
        pid = queue.groupPid[i];
        size = queue.groupSize[i];
        gid = queue.groupID[i];

#if PREDEFINED_ZOMBIE_TEST
        if (allowZombieBlocking &&
            !zombieGroupFinished && zombieTestOccupancy >= 1) {
            break;
        }
#endif

        allocatedTable = assignTable(state, isVip, size, gid, pid);
        if (allocatedTable != -1) {
            removeQueueItem(queue, i);
            break;
        }
    }
    V(SEM_MUTEX_QUEUE);

    if (allocatedTable == -1)
        return false;

    // If we successfully assigned, we consumed the queue item.
    // The Client had P(QueueFree), so now we have a free slot in the queue because this guy left it (entered table).
    // So we V(QueueFree) to allow a new guy in.
    V(semFree);

    char logBuffer[256];
    snprintf(logBuffer, sizeof(logBuffer),
        "\033[32m[%ld] [SERVICE]: QUEUE -> TABLE | tableID=%d pid=%d groupID=%d size=%d vipStatus=%d\033[0m",
        time(NULL), allocatedTable, pid, gid, size, isVip);
    fifoLog(logBuffer);

    ServiceRequest assigned{};
    assigned.mtype = pid;
    assigned.type = REQ_GROUP_ASSIGNED;
    assigned.extraData = allocatedTable;
    queueSendRequest(assigned);

    return true;
}

void tryAssignPendingGroups(RestaurantState* state) {
    bool assignedSomething;

    do {
        assignedSomething = false;

        // VIP first
        if (tryAssignFromQueue(
            state,
            state->vipQueue,
            true,
            SEM_QUEUE_USED_VIP,
            SEM_QUEUE_FREE_VIP,
            false
        )) {
            assignedSomething = true;
            continue; // Strict priority: if VIP served, re-check VIPs (starve Normal)
        }

        if (tryAssignFromQueue(
            state,
            state->normalQueue,
            false,
            SEM_QUEUE_USED_NORMAL,
            SEM_QUEUE_FREE_NORMAL,
            true
        )) {
            assignedSomething = true;
        }

    } while (assignedSomething);
}

void handleQueueGroup(const ClientRequest& req) {
    char logBuffer[256];
    
    // Client has already blocked on SEM_QUEUE_FREE_... so slot is guaranteed.
    // We just push to the internal array.
    bool queued = queuePush(req.pid, req.vipStatus, req.groupSize, req.groupID);
    
    if (queued) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE]: GROUP QUEUED | pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), req.pid, req.groupID, req.groupSize, req.vipStatus);
        fifoLog(logBuffer);
        return;
    }
    
    handleQueueGroup(req);
}

// Add static counter for admission control
static int globalAssignmentRequests = 0;
static bool admissionGateOpen = false;

void handleAssignGroup(RestaurantState* state, const ClientRequest& req) {
    char logBuffer[256];

    // --- ADMISSION CONTROL BARRIER ---
    // If FIXED_GROUP_COUNT is active, we COUNT requests but DO NOT assign 
    // --- ADMISSION CONTROL BARRIER ---
    // If FIXED_GROUP_COUNT is active, we check if ALL groups are created (globally).
    // The Client process increments state->totalGroupsCreated.
    if (FIXED_GROUP_COUNT > 0 && !admissionGateOpen) {
        
        P(SEM_MUTEX_STATE);
        int created = state->totalGroupsCreated;
        V(SEM_MUTEX_STATE);

        if (created < FIXED_GROUP_COUNT) {
            // Force Queue - do not attempt assignment yet
            handleQueueGroup(req);
            return;
        } else {
            // Threshold reached! Open gates.
            admissionGateOpen = true;
            snprintf(logBuffer, sizeof(logBuffer),
                "\033[32m[%ld] [SERVICE]: ALL %d GROUPS CREATED - OPENING ADMISSION GATES\033[0m",
                time(NULL), FIXED_GROUP_COUNT);
            fifoLog(logBuffer);
            
            // CRITICAL FIX: Immediately trigger processing of the waiting queue!
            // Otherwise, if the current request is forced to queue (Fairness), we deadlock.
            tryAssignPendingGroups(state);
        }
    }
    // ---------------------------------

    // --- FAIRNESS CHECK (Queue Priority) ---
    // If there are people waiting in the corresponding queue, this new group MUST wait too.
    bool mustQueue = false;
    P(SEM_MUTEX_QUEUE);
    if (req.vipStatus) {
        if (state->vipQueue.count > 0) mustQueue = true;
    } else {
        if (state->normalQueue.count > 0) mustQueue = true;
    }
    V(SEM_MUTEX_QUEUE);

    if (mustQueue) {
        snprintf(logBuffer, sizeof(logBuffer), "\033[32m[%ld] [SERVICE]: GROUP %d QUEUEING DUE TO FAIRNESS | vipQueue.count=%d normalQueue.count=%d", 
             time(NULL), req.groupID, state->vipQueue.count, state->normalQueue.count);
        fifoLog(logBuffer);
        handleQueueGroup(req);
        return;
    }

    P(SEM_MUTEX_STATE);
    int freeSeats = TOTAL_SEATS - state->currentGuestCount;

#if STRESS_TEST
    // ADMISSION CONTROL for Stress Test:
    // Clients must wait ("queue") until belt is full (500 items).
    // Check if belt is full (crude check: look for empty slots or assume check done outside).
    // Better: Count items on belt.
    int beltItems = 0;
    for(int i=0; i<BELT_SIZE; ++i) if(state->belt[i].dishID != 0) beltItems++;
    
    // Static state to track if we started allowing people (once opened, stay opened)
    static bool stressTestOpened = false;
    
    // Open floodgates if belt is full
    if (beltItems >= 500) stressTestOpened = true;
    
    if (!stressTestOpened) {
        V(SEM_MUTEX_STATE); // Release lock before queuing
        handleQueueGroup(req);
        return;
    }
#endif

    // ADMISSION CONTROL for Zombie Test:
#if PREDEFINED_ZOMBIE_TEST
    // Only 1 group allowed in the restaurant for Zombie Test UNTIL Group 0 finishes
    // Global static zombieTestOccupancy is used
    if (!zombieGroupFinished && zombieTestOccupancy >= 1) {
        V(SEM_MUTEX_STATE);
        handleQueueGroup(req); // Queue everyone else
        return;
    }
    zombieTestOccupancy++; 
#endif

    V(SEM_MUTEX_STATE);

    if (freeSeats < req.groupSize) {
        handleQueueGroup(req);
        return;
    }

    int assignedTable = assignTable(state, req.vipStatus, req.groupSize, req.groupID, req.pid);

    if (assignedTable != -1) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE]: TABLE ASSIGNED | tableID=%d pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), assignedTable, req.pid, req.groupID, req.groupSize, req.vipStatus);
        fifoLog(logBuffer);

        ServiceRequest assigned{};
        assigned.mtype = req.pid;
        assigned.type = REQ_GROUP_ASSIGNED;
        assigned.extraData = assignedTable;

        // CRITICAL: We successfully assigned a table instantly (skipping queue).
        // The Client has P(SEM_QUEUE_FREE) so they consumed a "waiting slot".
        // Since they are not waiting, we must return this slot to the system.
        if (req.vipStatus)
            V(SEM_QUEUE_FREE_VIP);
        else
            V(SEM_QUEUE_FREE_NORMAL);

        queueSendRequest(assigned);
        
        // If we just opened the gates, try to assign others too!
        if (admissionGateOpen) {
             tryAssignPendingGroups(state);
        }
        return;
    } else {
#if PREDEFINED_ZOMBIE_TEST
        P(SEM_MUTEX_STATE);
        zombieTestOccupancy--;
        V(SEM_MUTEX_STATE);
#endif
    }

    handleQueueGroup(req);
}

void handleGroupFinished(RestaurantState* state, const ClientRequest& req, int& finishedCount) {
    pid_t pid = req.pid;
    const int* eatenCount = req.eatenCount;

#if PREDEFINED_ZOMBIE_TEST
    // Decrement zombie occupancy counter so next group can enter (if any)
    P(SEM_MUTEX_STATE);
    zombieTestOccupancy--;
    if (req.groupID == 0) {
        zombieGroupFinished = true;
    }
    V(SEM_MUTEX_STATE);
#endif

    P(SEM_MUTEX_STATE);
    P(SEM_MUTEX_BELT);

    for (int i = 0; i < TABLE_COUNT; ++i) {
        Table& t = state->tables[i];

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != pid) continue;

            int groupID = req.groupID;
            int size = t.slots[s].size;
            bool vip = t.slots[s].vipStatus;

#if ZOMBIE_TEST == 1
            // Case 1: DO NOT clean zombie dishes
            // Logic skipped to simulate zombie dish accumulation
#else
            // Case 2 (or Normal): Clean zombie dishes
            cleanZombieDishes(state, groupID);
#endif

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
                "\033[32m[%ld] [SERVICE]: GROUP PAID OFF | groupID=%d pid=%d dishes=%d totalPrice=%d\033[0m",
                time(NULL), groupID, pid, groupDishes, groupRevenue);
            fifoLog(logBuffer);

            V(SEM_MUTEX_BELT);
            V(SEM_MUTEX_STATE);

            // Increment local finished count
            finishedCount++;
            
            // Check termination condition
            if (FIXED_GROUP_COUNT > 0 && finishedCount >= FIXED_GROUP_COUNT) {
                 snprintf(logBuffer, sizeof(logBuffer), 
                    "\033[32m[%ld] [SERVICE]: SERVED ALL GROUPS (%d) - INITIATING SHUTDOWN\033[0m", 
                    time(NULL), finishedCount);
                 fifoLog(logBuffer);
                 
                 // Signal parent (Main) to terminate
                 kill(getppid(), SIGINT);
            }

            tryAssignPendingGroups(state); // CHECK QUEUE after every departure
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
    
    // Local accounting for served groups
    int finishedGroups = 0;

    while (!terminate_flag && !evacuate_flag) {
        ClientRequest req{};
        queueRecvRequest(req);

        switch (req.type) {
        case REQ_ASSIGN_GROUP:
            handleAssignGroup(state, req);
            break;
        case REQ_BARRIER_CHECK:
            {
                // Force check barrier
                if (FIXED_GROUP_COUNT > 0 && !admissionGateOpen) {
                    P(SEM_MUTEX_STATE);
                    int created = state->totalGroupsCreated;
                    V(SEM_MUTEX_STATE);

                    if (created >= FIXED_GROUP_COUNT) {
                        admissionGateOpen = true;
                        char log[128];
                        snprintf(log, sizeof(log), 
                            "\033[32m[%ld] [SERVICE]: ALL %d GROUPS CREATED - OPENING ADMISSION GATES (SIGNAL)\033[0m",
                            time(NULL), FIXED_GROUP_COUNT);
                        fifoLog(log);
                        tryAssignPendingGroups(state);
                    }
                }
            }
            break;
        case REQ_GROUP_FINISHED:
            handleGroupFinished(state, req, finishedGroups);
            break;
        default:
            break;
        }
    }

    fifoCloseWrite();
}
