#include "ipc_manager.h"
#include "client.h"

#define ASSIGN 1
#define CONSUME 2
#define FINISHED 3

// ===== statyczne pola Group =====
int Group::nextGroupID = 0;

// ===== Handlery =====

static void handle_create_group() {
    Group g;
    pid_t pid = fork();
    if (pid == 0) {
        group_loop(g);
        _exit(0);
    }
}

void handle_group_finished(const Group& g) {
    ClientRequest req{};
    req.mtype = FINISHED;
    req.type = REQ_GROUP_FINISHED;
    req.pid = getpid();
    req.groupID = g.getGroupID();
    memcpy(req.eatenCount, g.getEatenCount(), sizeof(req.eatenCount));

    queue_send_request(req);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS] GROUP FINISHED | groupID=%d pid=%d\033[0m",
        time(NULL), g.getGroupID(), getpid());
    fifo_log(buf);
}

static void handle_get_group(const Group& g) {
    ClientResponse resp{};

    resp.mtype = getpid();
    resp.pid = getpid();
    resp.groupID = g.getGroupID();
    resp.groupSize = g.getGroupSize();
    resp.adultCount = g.getAdultCount();
    resp.childCount = g.getChildCount();
    resp.vipStatus = g.getVipStatus();
    resp.dishesToEat = g.getDishesToEat();

    queue_send_response(resp);
}

static void handle_reject_group(const Group& g) {
    char logBuffer[256];

    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP REJECTED pid=%d groupID=%d size=%d vip=%d dishes=%d\033[0m",
        time(NULL), getpid(), g.getGroupID(), g.getGroupSize(), g.getVipStatus(), g.getDishesToEat()
    );

    fifo_log(logBuffer);
    _exit(0);
}

void handle_table_assigned(Group& g, int tableIndex) {
    g.setTableIndex(tableIndex);
}

void handle_consume_dish(Group& g) {
    int tableIndex = g.getTableIndex();
    int groupID = g.getGroupID();
    int dishID = 0;
    colors color;
    int price = 0;

    P(SEM_MUTEX_BELT);

    Dish& d = state->belt[tableIndex];

    if (d.dishID != 0 && (d.targetGroupID == -1 || d.targetGroupID == groupID)) {
        P(SEM_BELT_ITEMS);

        g.consumeOneDish(d.color);

        dishID = d.dishID;
        color = d.color;
        price = d.price;

        // zdejmujemy z taśmy
        d.dishID = 0;
        d.targetGroupID = -1;
        V(SEM_BELT_SLOTS);
    }

    V(SEM_MUTEX_BELT);

    if (dishID != 0) {
        //usleep(200000 + rand() % 500000); // czas jedzenia

        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[38;5;118m[%ld] [CLIENTS]: CONSUMED %d DISH | table=%d, groupID=%d, pid=%d, color=%s, price=%d, dishesToEat=%d\033[0m",
            time(NULL), dishID, tableIndex, g.getGroupID(), getpid(),
            colorToString(color), price, g.getDishesToEat());
        fifo_log(logBuffer);
    }
    else {
        //usleep(1000);
    }
}

void* person_thread(void* arg) {
    PersonCtx* ctx = (PersonCtx*)arg;
    Group& g = *ctx->group;

    int groupID = g.getGroupID();
    int table = g.getTableIndex();
    int dishID = 0;
    colors color;
    int price;

    while (!g.isFinished() && !terminate_flag) {
        dishID = 0;

        if (g.orderPremiumDish()) {
            PremiumRequest order;
            order.mtype = 1;
            order.groupID = groupID;
            order.dish = rand() % 3 + 3;

            queue_send_request(order);
            
            char buf[256];
            snprintf(buf, sizeof(buf),
                "\033[38;5;118m[%ld] [CLIENT] PERSON %d ordered %s premium dish | group=%d table=%d ordersLeft=%d\033[0m",
                time(NULL), ctx->personID, colorToString(colorFromIndex(order.dish)), groupID, table, g.getOrdersLeft());
            fifo_log(buf);
        }

        handle_consume_dish(g);
    }
    return nullptr;
}

void group_loop(Group& g) {
    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);
    premium_qid = connect_queue(PREMIUM_REQ_QUEUE);

    // ===== zgłoszenie grupy =====
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

    queue_send_request(req);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "\033[38;5;118m[%ld] [CLIENTS] GROUP CREATED | id=%d pid=%d size=%d dishesToEat=%d ordersLeft=%d vipStatus=%d\033[0m",
        time(NULL), g.getGroupID(), getpid(), g.getGroupSize(), g.getDishesToEat(), g.getOrdersLeft(), g.getVipStatus());
    fifo_log(buf);

    // ===== oczekiwanie na stolik =====
    while (!terminate_flag) {
        ServiceRequest resp{};
        queue_recv_request(resp, getpid());

        if (resp.type == REQ_GROUP_ASSIGNED) {
            g.setTableIndex(resp.extraData);
            break;
        }

        if (resp.type == REQ_GROUP_REJECT)
            handle_reject_group(g);
    }

    // ===== start wątków =====
    int n = g.getGroupSize();
    pthread_t threads[n];
    PersonCtx ctx[n];

    for (int i = 0; i < n; ++i) {
        ctx[i] = { &g, i };
        pthread_create(&threads[i], nullptr, person_thread, &ctx[i]);
    }

    for (int i = 0; i < n; ++i)
        pthread_join(threads[i], nullptr);

    handle_group_finished(g);
    _exit(0);
}

// =====================================================
// GŁÓWNA PĘTLA CLIENT
// =====================================================

void start_clients() {
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    //signal(SIGRTMIN+1, terminate_handler);
    fifo_open_write();
    RestaurantState* state = get_state();

    while (!terminate_flag) {
        // losowy odstęp czasowy między przyjściem grup
        sleep(rand() % 3);

        handle_create_group();

        // Opcjonalnie log lub inne czynności w przerwie między generowaniem
    }

    fifo_close_write();
}
