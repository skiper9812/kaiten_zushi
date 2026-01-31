#include "service.h"

// Removes abandoned dishes from the belt if a group leaves prematurely
static void cleanZombieDishes(RestaurantState* state, int groupID) {
    for (int i = 0; i < BELT_SIZE; ++i) {
        Dish& d = state->belt[i];
        if (d.dishID == 0) continue;
        if (d.targetGroupID == groupID) {
            int colorIdx = colorToIndex(d.color);
            state->wastedCount[colorIdx]++;
            state->wastedValue[colorIdx] += d.price;

            d.dishID = 0;
            d.targetGroupID = -1;
            V(SEM_BELT_SLOTS);
        }
    }
}

static int zombieTestOccupancy = 0;
static bool zombieGroupFinished = false;

// Tries to find a suitable table for a group
// Returns table index or -1 if none found
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

        // Logic check: Can we merge with existing occupants? 
        // For simplicity: All groups at a table must be compatible (e.g., same size? No requirement in spec)
        // Spec says: "Groups can share tables".
        // Test IV: Mixing sizes. Ensure we don't start logical conflict.
        
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

#if TABLE_SHARING_TEST
        if (referenceSize != -1 && referenceSize != groupSize)
            compatible = false;
#endif

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

void removeQueueItem(GroupQueue& q, int index) {
    if (index < 0 || index >= q.count) return;
    for (int i = index; i < q.count - 1; ++i) {
        q.groupPid[i] = q.groupPid[i + 1];
        q.groupSize[i] = q.groupSize[i + 1];
        q.groupID[i] = q.groupID[i + 1];
    }
    q.count--;
}

// Attempts to assign waiting groups from the queue to tables
// Called when a table frees up or admission gates open
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

    V(semFree); // Release a spot in the queue backlog limiter

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

// Iteratively tries to seat groups from both VIP/Normal queues
void tryAssignPendingGroups(RestaurantState* state) {
    bool assignedSomething;

    do {
        assignedSomething = false;

        if (tryAssignFromQueue(
            state,
            state->vipQueue,
            true,
            SEM_QUEUE_USED_VIP,
            SEM_QUEUE_FREE_VIP,
            false
        )) {
            assignedSomething = true;
            continue;
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
    
    bool queued = queuePush(req.pid, req.vipStatus, req.groupSize, req.groupID);
    
    if (queued) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE]: GROUP QUEUED | pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), req.pid, req.groupID, req.groupSize, req.vipStatus);
        fifoLog(logBuffer);
        return;
    }
    
    // Queue full? Retry (should be protected by semaphore backpressure in Client, but strictly safe here)
    handleQueueGroup(req);
}

static int globalAssignmentRequests = 0;
static bool admissionGateOpen = false;

// Receives a new group assignment request from Client process
// Decides to Seat immediately, Queue, or Wait (if gated)
void handleAssignGroup(RestaurantState* state, const ClientRequest& req) {
    char logBuffer[256];

    // Gating Logic: Wait for all N groups to be created before letting ANYONE in (if requested)
    if (FIXED_GROUP_COUNT > 0 && !admissionGateOpen) {
        
        P(SEM_MUTEX_STATE);
        int created = state->totalGroupsCreated;
        V(SEM_MUTEX_STATE);

        if (created < FIXED_GROUP_COUNT) {
            handleQueueGroup(req);
            return;
        } else {
            admissionGateOpen = true;
            snprintf(logBuffer, sizeof(logBuffer),
                "\033[32m[%ld] [SERVICE]: ALL %d GROUPS CREATED - OPENING ADMISSION GATES\033[0m",
                time(NULL), FIXED_GROUP_COUNT);
            fifoLog(logBuffer);
            
            tryAssignPendingGroups(state);
        }
    }

    // Fairness check: If queue is non-empty, new groups MUST queue first
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
    // Custom Stress Test gate logic
    int beltItems = 0;
    for(int i=0; i<BELT_SIZE; ++i) if(state->belt[i].dishID != 0) beltItems++;
    
    static bool stressTestOpened = false;
    
    if (beltItems >= 500) stressTestOpened = true;
    
    if (!stressTestOpened) {
        V(SEM_MUTEX_STATE);
        handleQueueGroup(req);
        return;
    }
#endif

#if PREDEFINED_ZOMBIE_TEST
    if (!zombieGroupFinished && zombieTestOccupancy >= 1) {
        V(SEM_MUTEX_STATE);
        handleQueueGroup(req);
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

        if (req.vipStatus)
            V(SEM_QUEUE_FREE_VIP);
        else
            V(SEM_QUEUE_FREE_NORMAL);

        queueSendRequest(assigned);
        
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

// Processes a group that has finished eating
void handleGroupFinished(RestaurantState* state, const ClientRequest& req, int& finishedCount) {
    pid_t pid = req.pid;
    const int* eatenCount = req.eatenCount;

#if PREDEFINED_ZOMBIE_TEST
    P(SEM_MUTEX_STATE);
    zombieTestOccupancy--;
    if (req.groupID == 0) {
        zombieGroupFinished = true;
    }
    V(SEM_MUTEX_STATE);
#endif

    P(SEM_MUTEX_STATE);
    P(SEM_MUTEX_BELT);

    // Free up table slot
    for (int i = 0; i < TABLE_COUNT; ++i) {
        Table& t = state->tables[i];

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != pid) continue;

            int groupID = req.groupID;
            int size = t.slots[s].size;
            bool vip = t.slots[s].vipStatus;

#if ZOMBIE_TEST == 1
#else
            cleanZombieDishes(state, groupID);
#endif

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

            finishedCount++;
            
            // Check for termination condition trigger
            if (FIXED_GROUP_COUNT > 0 && finishedCount >= FIXED_GROUP_COUNT) {
                 snprintf(logBuffer, sizeof(logBuffer), 
                    "\033[32m[%ld] [SERVICE]: SERVED ALL GROUPS (%d) - INITIATING SHUTDOWN\033[0m", 
                    time(NULL), finishedCount);
                 fifoLog(logBuffer);
                 
                 kill(getppid(), SIGINT); // Trigger shutdown in Main
            }

            tryAssignPendingGroups(state);
            return;
        }
    }

    V(SEM_MUTEX_BELT);
    V(SEM_MUTEX_STATE);
}

// Main Service Process Loop
void startService() {
    fifoOpenWrite();

    clientQid = connectQueue(CLIENT_REQ_QUEUE);
    serviceQid = connectQueue(SERVICE_REQ_QUEUE);

    RestaurantState* state = getState();
    
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
