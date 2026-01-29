#include "ipc_manager.h"
#include "client.h"

#define ASSIGN 1
#define CONSUME 2
#define FINISHED 3
int parent = 1;

static void evacuationHandler(int sig) {
    evacuate_flag = 1;
}

static void terminateHandler(int sig) {
    terminate_flag = 1;
}

int Group::nextGroupID = 0;

static void* reaperThread(void* arg) {
    while (true) {
        int status;
        // Use WNOHANG after flags set to check for remaining children without blocking forever
        int options = (terminate_flag || evacuate_flag) ? WNOHANG : 0;
        pid_t pid = waitpid(-1, &status, options);
        
        if (pid == -1) {
            if (errno == ECHILD) break;  // No more children - done
            if (errno == EINTR) continue;  // Interrupted, retry
            break;  // Other error
        }
        
        if (pid == 0) {
            // WNOHANG returned with no children exited yet
            SIM_SLEEP(10000);  // Wait 10ms and check again
            continue;
        }
        // Child reaped, continue checking for more
    }
    return nullptr;
}

// Check if restaurant is closing (TP->TK time elapsed)
static bool isClosingTime() {
    RestaurantState* s = getState();
    if (s == nullptr) return false;
    return (time(NULL) - s->startTime) >= SIMULATION_DURATION_SECONDS;
}

static void handleCreateGroup() {
    // Don't create new groups if closing time
    if (isClosingTime() || terminate_flag || evacuate_flag) {
        return;
    }

    Group g;
    pid_t pid = fork();
    if (pid == 0) {
        parent = 0;
        struct sigaction sa = { 0 };
        sa.sa_handler = evacuationHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags &= ~SA_RESTART;
        sigaction(SIGTERM, &sa, NULL);

        struct sigaction saInt = { 0 };
        saInt.sa_handler = terminateHandler;
        sigemptyset(&saInt.sa_mask);
        saInt.sa_flags &= ~SA_RESTART;
        sigaction(SIGINT, &saInt, NULL);

        groupLoop(g);
        _exit(0);
    }
}

void handleGroupFinished(Group& g) {
    ClientRequest req{};
    req.mtype = FINISHED;
    req.type = REQ_GROUP_FINISHED;
    req.pid = getpid();
    req.groupID = g.getGroupID();
    g.getEatenCount(req.eatenCount);

    queueSendRequest(req);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS] GROUP FINISHED | groupID=%d pid=%d\033[0m",
        time(NULL), g.getGroupID(), getpid());
    fifoLog(buf);
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
    handleGroupFinished(g);
    char logBuffer[256];

    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP REJECTED pid=%d groupID=%d size=%d vip=%d dishes=%d\033[0m",
        time(NULL), getpid(), g.getGroupID(), g.getGroupSize(), g.getVipStatus(), g.getDishesToEat()
    );

    fifoLog(logBuffer);
    
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
            g.consumeOneDish(d.color);

            dishID = d.dishID;
            beltSlot = i;
            color = d.color;
            price = d.price;

            // IMMEDIATELY track consumed dish in shared memory (before process could be killed)
            int colorIdx = colorToIndex(d.color);
            state->soldCount[colorIdx]++;
            state->soldValue[colorIdx] += d.price;
            state->revenue += d.price;

            d.dishID = 0;
            d.targetGroupID = -1;

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
            "\033[38;5;118m[%ld] [CLIENTS]: CONSUMED DISH %d | beltSlot=%d groupID=%d pid=%d color=%s price=%d dishesToEat=%d\033[0m",
            time(NULL),
            dishID,
            beltSlot,
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
    int dishID = 0;
    colors color;
    int price;

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
                "\033[38;5;118m[%ld] [CLIENT] PERSON %d ordered %s premium dish | group=%d table=%d ordersLeft=%d\033[0m",
                time(NULL), ctx->personID, colorToString(colorFromIndex(order.dish)), groupID, table, g.getOrdersLeft());
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

    queueSendRequest(req);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS] GROUP CREATED | id=%d pid=%d size=%d dishesToEat=%d ordersLeft=%d vipStatus=%d\033[0m",
        time(NULL), g.getGroupID(), getpid(), g.getGroupSize(), g.getDishesToEat(), g.getOrdersLeft(), g.getVipStatus());
    fifoLog(buf);

    while (!terminate_flag && !evacuate_flag) {
        ServiceRequest resp{};
        queueRecvRequest(resp, getpid());

        if (resp.type == REQ_GROUP_ASSIGNED) {
            g.setTableIndex(resp.extraData);
            break;
        }

        if (resp.type == REQ_GROUP_REJECT)
            handleRejectGroup(g);
    }

    int n = g.getGroupSize();
    pthread_t threads[n];
    PersonCtx ctx[n];

    for (int i = 0; i < n; ++i) {
        ctx[i] = { &g, i };
        pthread_create(&threads[i], nullptr, personThread, &ctx[i]);
    }

    for (int i = 0; i < n; ++i)
        pthread_join(threads[i], nullptr);

    handleGroupFinished(g);
    _exit(0);
}

// =====================================================
// MAIN CLIENT LOOP
// =====================================================

void startClients() {
    fifoOpenWrite();
    RestaurantState* state = getState();

    pthread_t reaper;
    pthread_create(&reaper, nullptr, reaperThread, nullptr);

    while (!terminate_flag && !evacuate_flag) {
        // Check for closing time
        if (isClosingTime()) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "\033[33m[%ld] [CLIENTS]: CLOSING TIME - no more groups accepted\033[0m",
                time(NULL));
            fifoLog(buf);
            break;
        }

        SIM_SLEEP(rand() % 100000);
        handleCreateGroup();
    }

    // Wait for all existing groups to finish
    pthread_join(reaper, nullptr);

    fifoCloseWrite();
}
