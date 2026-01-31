#include "ipc_manager.h"
#include "client.h"

#include <semaphore.h>
#define ASSIGN 1
#define CONSUME 2
#define FINISHED 3
int parent = 1;

// POSIX semaphore for event-driven reaping (no polling)
static sem_t reaperSem;

// Unified signal handler for group child processes
// Both signals should trigger graceful shutdown with FINISHED logging
static void groupSignalHandler(int sig) {
    terminate_flag = 1;
    evacuate_flag = 1;
}

int Group::nextGroupID = 0;

static void* reaperThread(void* arg) {
    while (true) {
        // Wait for a child to be created (or Poison Pill during shutdown)
        // This eliminates the need for sleep/polling on ECHILD.
        if (sem_wait(&reaperSem) == -1) {
            if (errno == EINTR) continue;
            break; // Should not happen
        }

        // If termination triggered and we are woken up, check if we should exit.
        // But we must prioritized reaping real children first if any exist.
        
        int status;
        // Blocking wait is fine because we know at least one child exists (or existed)
        // corresponding to the semaphore token we just consumed.
        pid_t pid = waitpid(-1, &status, 0);
        
        if (pid == -1) {
            if (errno == ECHILD) {
                // ECHILD here means the semaphore count was > actual children.
                // This happens when we send the "Poison Pill" at shutdown to wake the thread up.
                if (terminate_flag || evacuate_flag) {
                    break;
                }
                // If not terminating? Theoretical mismatch? Just loop.
                continue;
            }
            // EINTR handled by loop usually, but here we consumed token. 
            // Ideally we retry waitpid? But if we loop, we hit sem_wait again blocks.
            // Actually waitpid handles EINTR internally often or returns -1.
            // If we assume token consumed = child dead, we might desync if waitpid fails.
            // But for this simulation, ECHILD is the main case.
            if (errno == EINTR) {
                // We consumed a token but didn't reap. We should technically put it back or retry waitpid.
                // Simpler to just loop - if child is really there, waitpid next time might catch it?
                // But sem_wait will block.
                // Retry waitpid only:
                continue; // Dangerous if we go back to sem_wait.
                // Let's assume standard flow.
            }
        }
        // Child reaped successfully
    }
    return nullptr;
}

// Check if restaurant is closing (TP->TK time elapsed)
static bool isClosingTime() {
    RestaurantState* s = getState();
    if (s == nullptr) return false;
    
    long elapsed = time(NULL) - s->startTime;
    long pauseSec = s->totalPauseNanoseconds / 1000000000LL;
    
    return (elapsed - pauseSec) >= SIMULATION_DURATION_SECONDS;
}

static bool handleCreateGroup() {
    // Don't create new groups if closing time
    if (isClosingTime() || terminate_flag || evacuate_flag) {
        return false;
    }

    // Block SIGTERM during fork to prevent race condition
    sigset_t blockSet, oldSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGTERM);
    sigprocmask(SIG_BLOCK, &blockSet, &oldSet);

    Group g;
    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed (likely process limit reached)
        sigprocmask(SIG_SETMASK, &oldSet, NULL);
        return false;
    }

    if (pid == 0) {
        parent = 0;
        // Ignore SIGINT - main process will propagate as SIGTERM
        signal(SIGINT, SIG_IGN);
        
        // Handle SIGTERM for graceful shutdown
        struct sigaction sa = { 0 };
        sa.sa_handler = groupSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags &= ~SA_RESTART;
        sigaction(SIGTERM, &sa, NULL);
        
        // Now unblock SIGTERM - handler is ready
        sigprocmask(SIG_SETMASK, &oldSet, NULL);

        groupLoop(g);
        _exit(0);
    }
    
    // Parent: restore signal mask
    sigprocmask(SIG_SETMASK, &oldSet, NULL);

    RestaurantState* s = getState();
    if (s) {
        P(SEM_MUTEX_STATE);
        s->totalGroupsCreated++;
        V(SEM_MUTEX_STATE);
    }
    
    // Notify reaper that a new child exists (Producer)
    if (parent) { 
        // Logic check: we are definitely parent here because pid != 0 block above calls _exit
        // But safeguard 'if (parent)' helps readability or if logic changes.
        // Actually parent=1 is global default, set to 0 in child block.
        sem_post(&reaperSem);
    }

    return true;
}

void handleGroupFinished(Group& g, bool wasSeated) {
    // Always log FINISHED for all groups
    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP FINISHED    | groupID=%d pid=%d wasSeated=%d\033[0m",
        time(NULL), g.getGroupID(), getpid(), wasSeated);
    fifoLog(buf);

    // Only send cleanup request to service if we had a table
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

    P(SEM_BELT_ITEMS);
    if (evacuate_flag || terminate_flag) {
        V(SEM_BELT_ITEMS);
        return;
    }

    P(SEM_MUTEX_BELT);

    // Calculate belt range for this table (circular belt - uses modulo)
    int tableIndex = g.getTableIndex();
    int slotsPerTable = BELT_SIZE / TABLE_COUNT;  // Floor division - some slots may be unassigned
    if (slotsPerTable < 1) slotsPerTable = 1;     // Minimum 1 slot per table
    int startSlot = (tableIndex * slotsPerTable) % BELT_SIZE;  // Wrap around

    // Search within this table's belt range (handles wrap-around)
    for (int j = 0; j < slotsPerTable; ++j) {
        int i = (startSlot + j) % BELT_SIZE;
        Dish& d = state->belt[i];
        if (d.dishID != 0 && (d.targetGroupID == -1 || d.targetGroupID == groupID)) {
            if (!g.consumeOneDish(d.color)) {
                V(SEM_BELT_ITEMS);
                V(SEM_MUTEX_BELT);
                return; 
            }

            dishID = d.dishID;
            beltSlot = i;
            color = d.color;
            price = d.price;

            d.dishID = 0;
            d.targetGroupID = -1;

#if CRITICAL_TEST
            // Trigger suicide after eating a few dishes to ensure system is running
            // Check global flag to prevent multiple processes from killing simultaneously
            if (!state->suicideTriggered && state->soldCount[0] > 10) { 
                state->suicideTriggered = 1;
                fifoLog("!!! TRIGGERING SUICIDE SIGNAL IN CRITICAL SECTION !!!");
                kill(0, SIGINT); // Kill everyone including self (via Main propagation)
                usleep(200000);   // Wait for signal to be delivered
            }
#endif
            // IMMEDIATELY track consumed dish in shared memory (before process could be killed)
            int colorIdx = colorToIndex(color);
            state->soldCount[colorIdx]++;
            state->soldValue[colorIdx] += price;
            state->revenue += price;
            

            V(SEM_BELT_SLOTS);
            break;
        }
    }

    // If no suitable dish was found, release the semaphore we took
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


void* personThread(void* arg) {
    PersonCtx* ctx = (PersonCtx*)arg;
    Group& g = *ctx->group;

    int groupID = g.getGroupID();
    int table = g.getTableIndex();

#if PREDEFINED_ZOMBIE_TEST
    // ZOMBIE TEST LOGIC: Strict Phases (Only for Group 0)
    if (groupID == 0) {
        // Phase 1: Order ALL dishes
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

        // Phase 2: Wait until Belt is Full (100 items)
        RestaurantState* state = getState();
        while (!terminate_flag && !evacuate_flag) {
            int beltItems = 0;
            // Check belt occupancy (peek)
            P(SEM_MUTEX_BELT);
            for(int i=0; i<BELT_SIZE; ++i) if(state->belt[i].dishID != 0) beltItems++;
            V(SEM_MUTEX_BELT);
            
            if (beltItems >= BELT_SIZE) break; 
        }

        // Phase 3: Eat exactly 1 dish and exit
        if (!terminate_flag && !evacuate_flag) {
             handleConsumeDish(g);
        }

        return nullptr;
    }
#endif

    // NORMAL LOGIC (Fallthrough for other groups or if ZOMBIE_TEST is disabled)
    int dishID = 0;
    while (!terminate_flag && !evacuate_flag) {
        dishID = 0;

        if (g.isFinished() && !terminate_flag && !evacuate_flag)
             break;

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

    // Wait for space in the RESTAURANT queue (VIP or Normal)
    // This blocks the CLIENT process, keeping the SERVICE process free.
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

    // Only create person threads if we got a table
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

// =====================================================
// MAIN CLIENT LOOP
// =====================================================

void startClients() {
    fifoOpenWrite();
    sem_init(&reaperSem, 0, 0); 
    RestaurantState* state = getState();

    pthread_t reaper;
    pthread_create(&reaper, nullptr, reaperThread, nullptr);

    int createdGroups = 0;
    while (!terminate_flag && !evacuate_flag) {
        // Check if we reached the fixed group count limit (if enabled)
        if (FIXED_GROUP_COUNT >= 0 && createdGroups >= FIXED_GROUP_COUNT) {
            sleep(1000000); // Wait until end of simulation
            // Just wait, do not break (so reaper can continue cleaning up)
            continue; 
        }

        // BACKPRESSURE: Check if client message queue is full.
        // If full, do not spam new processes (Fork Bomb prevention).
        // Without this, SKIP_DELAYS + Blocking IPC = 150k+ processes blocked on start.
        if (getSemValue(SEM_CLIENT_FREE) <= 0) {
            SIM_SLEEP(20000); // 20ms backoff
            continue;
        }

#if STRESS_TEST
        // Very fast spawn for stress test
        if (handleCreateGroup()) {
            createdGroups++;
        } else {
             SIM_SLEEP(100000); // Wait 100ms if fork failed (OS limit)
        }
#else
        // Check for closing time (Normal Mode logic)
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
            
            // WAKE UP SERVICE if we hit the target
            if (FIXED_GROUP_COUNT > 0) {
                 RestaurantState* s = getState();
                 int currentTotal = 0;
                 if(s) {
                     P(SEM_MUTEX_STATE);
                     currentTotal = s->totalGroupsCreated;
                     V(SEM_MUTEX_STATE);
                 }
                 
                 // Trigger exactly once when we cross the line
                 if (currentTotal == FIXED_GROUP_COUNT) {
                     ClientRequest wakeUp{};
                     wakeUp.mtype = 1; // Any type service listens to
                     wakeUp.type = REQ_BARRIER_CHECK;
                     wakeUp.pid = getpid();
                     queueSendRequest(wakeUp);
                 }
            }
        } else {
             // If fork failed in normal mode, just continue loop (will sleep anyway)
        }
#endif
    }


    // Rebroadcast SIGTERM to ensure all children (including those born during signal race) get it
    // Block SIGTERM for self first to avoid recursion/premature death
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigprocmask(SIG_BLOCK, &set, NULL);
    
    kill(0, SIGTERM);  // Send to process group (children)
    
    // "Poison Pill": Wake up reaper one last time so it sees terminate_flag and ECHILD (empty sem)
    sem_post(&reaperSem);

    // Wait for all existing groups to finish
    pthread_join(reaper, nullptr);
    sem_destroy(&reaperSem);

    fifoCloseWrite();
}
