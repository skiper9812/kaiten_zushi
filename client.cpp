#include "ipc_manager.h"
#include "client.h"

#include <semaphore.h>
#define ASSIGN 1
#define CONSUME 2
#define FINISHED 3
int parent = 1; // Flag to distinguish parent (Client Manager) from children (Groups)

static sem_t reaperSem; // Event semaphore for reaper thread

// Handler for group process termination signals
static void groupSignalHandler(int sig) {
    terminate_flag = 1;
    evacuate_flag = 1;
}

int Group::nextGroupID = 0;

// Async thread to reap child processes (prevents zombies)
// Uses semaphore to wake up only when needed (Event-driven)
static void* reaperThread(void* arg) {
    while (true) {
        // Wait for signal that a child was created or termination requested
        if (sem_wait(&reaperSem) == -1) {
            if (errno == EINTR) continue;
            break;
        }

        int status;
        // Wait for any child process to change state
        pid_t pid = waitpid(-1, &status, 0);
        
        if (pid == -1) {
            if (errno == ECHILD) {
                if (terminate_flag || evacuate_flag) {
                    break;
                }
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
        }
    }
    return nullptr;
}

// Checks if simulation time has exceeded the limit
static bool isClosingTime() {
    RestaurantState* s = getState();
    if (s == nullptr) return false;
    
    long elapsed = time(NULL) - s->startTime;
    long pauseSec = s->totalPauseNanoseconds / 1000000000LL;
    
    return (elapsed - pauseSec) >= SIMULATION_DURATION_SECONDS;
}

// Creates a new group process (fork)
static bool handleCreateGroup() {
    if (isClosingTime() || terminate_flag || evacuate_flag) {
        return false;
    }

    sigset_t blockSet, oldSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGTERM);
    sigprocmask(SIG_BLOCK, &blockSet, &oldSet);

    Group g;
    pid_t pid = fork();
    if (CHECK_ERR(pid, ERR_IPC_INIT, "fork group failed") != ERR_DECISION_IGNORE) {
        sigprocmask(SIG_SETMASK, &oldSet, NULL);
        return false;
    }

    if (pid == 0) {
        // Child Process
        parent = 0;
        signal(SIGINT, SIG_IGN);
        
        struct sigaction sa = { 0 };
        sa.sa_handler = groupSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags &= ~SA_RESTART;
        sigaction(SIGTERM, &sa, NULL);
        
        sigprocmask(SIG_SETMASK, &oldSet, NULL);

        groupLoop(g);
        _exit(0);
    }
    
    // Parent Process
    sigprocmask(SIG_SETMASK, &oldSet, NULL);

    RestaurantState* s = getState();
    if (s) {
        P(SEM_MUTEX_STATE);
        s->totalGroupsCreated++;
        V(SEM_MUTEX_STATE);
    }
    
    // Notify reaper that a new child exists
    if (parent) { 
        sem_post(&reaperSem);
    }

    return true;
}

// Sends a message to Service that group has finished eating
void handleGroupFinished(Group& g, bool wasSeated) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP FINISHED    | groupID=%d pid=%d wasSeated=%d\033[0m",
        time(NULL), g.getGroupID(), getpid(), wasSeated);
    fifoLog(buf);

    if (!wasSeated)
        return;

    ClientRequest req{};
    req.mtype = FINISHED;
    req.type = REQ_GROUP_FINISHED;
    req.pid = getpid();
    req.groupID = g.getGroupID();
    g.getEatenCount(req.eatenCount);
    queueSendRequest(req);
}

static void handleGetGroup(Group& g) {
    ClientResponse resp{};

    resp.mtype = getpid();
    resp.pid = getpid();
    resp.groupID = g.getGroupID();
    resp.groupSize = g.getGroupSize();
    resp.adultCount = g.getAdultCount();
    resp.childCount = g.getChildCount();
    resp.vipStatus = g.getVipStatus();
    resp.dishesToEat = g.getDishesToEat();

    queueSendResponse(resp);
}

// Handle case where group is rejected by service (e.g., no room)
static void handleRejectGroup(Group& g) {
    char logBuffer[256];

    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP REJECTED pid=%d groupID=%d size=%d vip=%d dishes=%d\033[0m",
        time(NULL), getpid(), g.getGroupID(), g.getGroupSize(), g.getVipStatus(), g.getDishesToEat()
    );
    fifoLog(logBuffer);

    handleGroupFinished(g, false);
    _exit(0);
}

void handleTableAssigned(Group& g, int tableIndex) {
    g.setTableIndex(tableIndex);
}

// Logic to find and consume a dish from the conveyor belt
void handleConsumeDish(Group& g) {
    if (evacuate_flag || terminate_flag)
        return;

    int groupID = g.getGroupID();
    int dishID = 0;
    int beltSlot = -1;
    colors color;
    int price = 0;

    if (evacuate_flag || terminate_flag)
        return;

    // Check availability of items on belt (Semaphore check)
    P(SEM_BELT_ITEMS);
    if (evacuate_flag || terminate_flag) {
        V(SEM_BELT_ITEMS);
        return;
    }

    P(SEM_MUTEX_BELT);

    // Calculate accessible slots based on table assignment
    int tableIndex = g.getTableIndex();
    int slotsPerTable = BELT_SIZE / TABLE_COUNT;
    if (slotsPerTable < 1) slotsPerTable = 1;
    int startSlot = (tableIndex * slotsPerTable) % BELT_SIZE;

    // Scan belt slots visible to this table
    for (int j = 0; j < slotsPerTable; ++j) {
        int i = (startSlot + j) % BELT_SIZE;
        Dish& d = state->belt[i];
        if (d.dishID != 0 && (d.targetGroupID == -1 || d.targetGroupID == groupID)) {
            // Attempt to consume
            if (!g.consumeOneDish(d.color)) {
                // Determine if we should skip or take
                V(SEM_BELT_ITEMS);
                V(SEM_MUTEX_BELT);
                return; 
            }

            // Take the dish
            dishID = d.dishID;
            beltSlot = i;
            color = d.color;
            price = d.price;

            d.dishID = 0;
            d.targetGroupID = -1;

#if CRITICAL_TEST
            if (!state->suicideTriggered && state->soldCount[0] > 10) { 
                state->suicideTriggered = 1;
                fifoLog("!!! TRIGGERING SUICIDE SIGNAL IN CRITICAL SECTION !!!");
                kill(0, SIGINT);
                usleep(200000);
            }
#endif
            // Update stats
            int colorIdx = colorToIndex(color);
            state->soldCount[colorIdx]++;
            state->soldValue[colorIdx] += price;
            state->revenue += price;
            
            V(SEM_BELT_SLOTS); // Slot is now free
            break;
        }
    }

    // If no dish was taken, restore item count semaphore
    if (dishID == 0) {
        V(SEM_BELT_ITEMS);
    }

    V(SEM_MUTEX_BELT);

    if (dishID != 0) {
        char logBuffer[256];
        snprintf(
            logBuffer,
            sizeof(logBuffer),
            "\033[38;5;118m[%ld] [CLIENTS]: CONSUMED DISH %d | beltSlot=%d tableID=%d groupID=%d pid=%d color=%s price=%d dishesToEat=%d\033[0m",
            time(NULL),
            dishID,
            beltSlot,
            tableIndex,
            groupID,
            getpid(),
            colorToString(color),
            price,
            g.getDishesToEat()
        );
        fifoLog(logBuffer);
    }
}


// Thread representing a single person in a group
void* personThread(void* arg) {
    PersonCtx* ctx = (PersonCtx*)arg;
    Group& g = *ctx->group;

    int groupID = g.getGroupID();
    int table = g.getTableIndex();

#if PREDEFINED_ZOMBIE_TEST
    // Specialized logic for Zombie Test case
    if (groupID == 0) {
        // Order all food first without eating
        while (!terminate_flag && !evacuate_flag && g.getOrdersLeft() > 0) {
            if (g.orderPremiumDish()) {
                PremiumRequest order;
                order.mtype = 1;
                order.groupID = groupID;
                order.dish = rand() % 3 + 3;
                queueSendRequest(order);
                
                char buf[256];
                snprintf(buf, sizeof(buf), "\033[38;5;118m[%ld] [CLIENT] ZOMBIE ORDER | ordersLeft=%d\033[0m", time(NULL), g.getOrdersLeft());
                fifoLog(buf);
            }
        }

        RestaurantState* state = getState();
        // Wait until belt is full
        while (!terminate_flag && !evacuate_flag) {
            int beltItems = 0;
            P(SEM_MUTEX_BELT);
            for(int i=0; i<BELT_SIZE; ++i) if(state->belt[i].dishID != 0) beltItems++;
            V(SEM_MUTEX_BELT);
            
            if (beltItems >= BELT_SIZE) break; 
        }

        if (!terminate_flag && !evacuate_flag) {
             handleConsumeDish(g);
        }

        return nullptr;
    }
#endif

    int dishID = 0;
    while (!terminate_flag && !evacuate_flag) {
        dishID = 0;

        if (g.isFinished() && !terminate_flag && !evacuate_flag)
             break;

        // Try ordering premium dish
        if (g.orderPremiumDish()) {
            PremiumRequest order;
            order.mtype = 1;
            order.groupID = groupID;
            order.dish = rand() % 3 + 3;

            queueSendRequest(order);
            
            char buf[256];
            snprintf(buf, sizeof(buf),
                "\033[38;5;118m[%ld] [CLIENTS]: PERSON %d ORDERED PREMIUM DISH | groupID=%d tableID=%d color=%s ordersLeft=%d\033[0m",
                time(NULL), ctx->personID, groupID, table, colorToString(colorFromIndex(order.dish)), g.getOrdersLeft());
            fifoLog(buf);
        }

        handleConsumeDish(g);
    }
    return nullptr;
}

// Lifecycle of a Group process
void groupLoop(Group& g) {
    clientQid = connectQueue(CLIENT_REQ_QUEUE);
    serviceQid = connectQueue(SERVICE_REQ_QUEUE);
    premiumQid = connectQueue(PREMIUM_REQ_QUEUE);

    ClientRequest req{};
    req.mtype = ASSIGN;
    req.type = REQ_ASSIGN_GROUP;
    req.pid = getpid();
    req.groupID = g.getGroupID();
    req.groupSize = g.getGroupSize();
    req.adultCount = g.getAdultCount();
    req.childCount = g.getChildCount();
    req.vipStatus = g.getVipStatus();
    memset(req.eatenCount, 0, sizeof(req.eatenCount));

    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP CREATED | groupID=%d pid=%d size=%d dishesToEat=%d ordersLeft=%d vipStatus=%d\033[0m",
        time(NULL), g.getGroupID(), getpid(), g.getGroupSize(), g.getDishesToEat(), g.getOrdersLeft(), g.getVipStatus());
    fifoLog(buf);

    // Admission Control (Backpressure)
    if (g.getVipStatus()) {
        P(SEM_QUEUE_FREE_VIP);
    } else {
        P(SEM_QUEUE_FREE_NORMAL);
    }
    
    if (terminate_flag || evacuate_flag) {
        if (g.getVipStatus()) V(SEM_QUEUE_FREE_VIP); else V(SEM_QUEUE_FREE_NORMAL);
    }

    queueSendRequest(req);

    bool wasSeated = false;

    // Wait for table assignment
    while (!terminate_flag && !evacuate_flag) {
        ServiceRequest resp{};
        queueRecvRequest(resp, getpid());

        if (resp.type == REQ_GROUP_ASSIGNED) {
            g.setTableIndex(resp.extraData);
            wasSeated = true;
            break;
        }

        if (resp.type == REQ_GROUP_REJECT)
            handleRejectGroup(g);
    }

    if (wasSeated) {
        int n = g.getGroupSize();
        pthread_t threads[n];
        PersonCtx ctx[n];

        for (int i = 0; i < n; ++i) {
            ctx[i] = { &g, i };
            pthread_create(&threads[i], nullptr, personThread, &ctx[i]);
        }

        for (int i = 0; i < n; ++i)
            pthread_join(threads[i], nullptr);
    }

    handleGroupFinished(g, wasSeated);
    _exit(0);
}

// Main Client Process loop
// Spawns new groups and manages reaper thread
void startClients() {
    fifoOpenWrite();
    sem_init(&reaperSem, 0, 0); 
    RestaurantState* state = getState();

    pthread_t reaper;
    pthread_create(&reaper, nullptr, reaperThread, nullptr);

    int createdGroups = 0;
    while (!terminate_flag && !evacuate_flag) {
        if (FIXED_GROUP_COUNT >= 0 && createdGroups >= FIXED_GROUP_COUNT) {
            sleep(1000000); // Wait for simulation to end
            continue; 
        }

        // Admission throttle (Global limit)
        if (getSemValue(SEM_CLIENT_FREE) <= 0) {
            SIM_SLEEP(20000);
            continue;
        }

#if STRESS_TEST
        if (handleCreateGroup()) {
            createdGroups++;
        } else {
             SIM_SLEEP(100000);
        }
#else
        if (isClosingTime()) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "\033[33m[%ld] [CLIENTS]: RESTAURANT IS CLOSING\033[0m",
                time(NULL));
            fifoLog(buf);
            break;
        }

        SIM_SLEEP(rand() % 100000);
        if (handleCreateGroup()) {
            createdGroups++;
            
            if (FIXED_GROUP_COUNT > 0) {
                 RestaurantState* s = getState();
                 int currentTotal = 0;
                 if(s) {
                     P(SEM_MUTEX_STATE);
                     currentTotal = s->totalGroupsCreated;
                     V(SEM_MUTEX_STATE);
                 }
                 
                 // Signal Service if Last Group created
                 if (currentTotal == FIXED_GROUP_COUNT) {
                     ClientRequest wakeUp{};
                     wakeUp.mtype = 1;
                     wakeUp.type = REQ_BARRIER_CHECK;
                     wakeUp.pid = getpid();
                     queueSendRequest(wakeUp);
                 }
            }
        }
#endif
    }


    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigprocmask(SIG_BLOCK, &set, NULL);
    
    kill(0, SIGTERM);
    
    // Resume reaper to ensure it exits cleanly
    sem_post(&reaperSem);

    pthread_join(reaper, nullptr);
    sem_destroy(&reaperSem);

    fifoCloseWrite();
}
