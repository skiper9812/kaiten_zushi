#include "client.h"
#include "ipc_manager.h"

#define ASSIGN 1
#define CONSUME 2
#define DONE 3

// ===== Handlery =====

static void handle_create_group() {
    char logBuffer[256];
    time_t timestamp = time(NULL);

    // Tworzenie nowego procesu grupy i inicjalizacja przez konstruktor
    Group g;  // konstruktor ustawia wszystkie pola
    int groupID = g.getGroupID();

    pid_t pid = fork();
    if (pid == 0) {
        // Proces potomny grupy
        group_loop(g); // lokalna pętla symulacji grupy
        _exit(0);
    }
}

void handle_group_done(const Group& g) {
    char logBuffer[256];
    time_t timestamp = time(NULL);

    // Tworzymy komunikat do service
    ClientRequest req{};
    req.mtype = DONE;                  // typ wiadomości w kolejce
    req.type = REQ_GROUP_DONE;      // żądanie zakończenia
    req.pid = getpid();
    req.groupID = g.getGroupID();
    memcpy(req.eatenCount, g.getEatenCount(), sizeof(req.eatenCount));

    queue_send_request(req);         // wysyłamy do service

    // Logujemy intencję zakończenia
    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: REQUEST DONE | groupID=%d, pid=%d\033[0m",
        timestamp, g.getGroupID(), getpid());
    fifo_log(logBuffer);
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
    time_t timestamp = time(NULL);

    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP REJECTED pid=%d groupID=%d size=%d vip=%d dishes=%d\033[0m",
        timestamp, getpid(), g.getGroupID(), g.getGroupSize(), g.getVipStatus(), g.getDishesToEat()
    );

    fifo_log(logBuffer);

    _exit(0);
}

void handle_table_assigned(Group& g, int tableIndex) {
    g.setTableIndex(tableIndex);
}

void handle_consume_dish(Group& g) {
    int tableIndex = g.getTableIndex();
    int dishID = 0;
    colors color;
    int price = 0;

    P(SEM_MUTEX_BELT);
    Dish& d = state->belt[tableIndex];

    if (d.dishID != 0) {
        P(SEM_BELT_ITEMS);
        g.consumeOneDish(d.color);

        dishID = d.dishID;
        color = d.color;
        price = d.price;
        
        d.dishID = 0;
        V(SEM_BELT_SLOTS);
    }

    V(SEM_MUTEX_BELT);

    if (dishID != 0) {
        char logBuffer[256];
        time_t timestamp = time(NULL);
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[38;5;118m[%ld] [CLIENTS]: CONSUMED %d DISH | table=%d, groupID=%d, pid=%d, color=%s, price=%d, dishesToEat=%d\033[0m",
            timestamp, dishID, g.getTableIndex(), g.getGroupID(), getpid(), colorToString(color), price, g.getDishesToEat());
        fifo_log(logBuffer);
    }
}

// ===== statyczne pola Group =====
int Group::nextGroupID = 0;

void group_loop(Group& g) {
    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);

    char logBuffer[256];
    time_t timestamp = time(NULL);

    // ===== Powiadomienie service.cpp, że nowa grupa przyszła =====
    ClientRequest request{};
    request.mtype = ASSIGN;          // typ wiadomości w kolejce (dowolnie ustalony)
    request.type = REQ_ASSIGN_GROUP;         // typ żądania CREATE_GROUP
    request.pid = getpid();            // PID procesu grupy
    request.groupID = g.getGroupID();
    request.groupSize = g.getGroupSize();
    request.adultCount = g.getAdultCount();
    request.childCount = g.getChildCount();
    request.vipStatus = g.getVipStatus();
    memset(request.eatenCount, 0, sizeof(request.eatenCount));

    queue_send_request(request);

    // Logowanie powstania grupy
    snprintf(logBuffer, sizeof(logBuffer),
        "\033[38;5;118m[%ld] [CLIENTS]: GROUP CREATED | groupID=%d, pid=%d, groupSize=%d, dishesLeft=%d, vipStatus=%d\033[0m",
        timestamp, g.getGroupID(), getpid(), g.getGroupSize(), g.getDishesToEat(), g.getVipStatus());
    fifo_log(logBuffer);

    while (true) {
        ServiceRequest req{};
        queue_recv_request(req, getpid());

        if (req.type == REQ_GROUP_ASSIGNED) {
            handle_table_assigned(g, req.extraData);
            break;
        }

        if (req.type == REQ_GROUP_REJECT) {
            handle_reject_group(g);
            _exit(0);
        }

        if (req.type == REQ_GET_GROUP) {
            handle_get_group(g);
        }
    }

    while (true) {
        if (g.isFinished()) {
            handle_group_done(g);
            break;
        }

        handle_consume_dish(g);

        ServiceRequest req{};
        queue_recv_request(req, getpid());

        switch (req.type) {
        case REQ_GET_GROUP:
            handle_get_group(g);
            break;
        default:
            break;
        }

        usleep(1);
    }

}

// =====================================================
// GŁÓWNA PĘTLA CLIENT
// =====================================================

void start_clients() {
    signal(SIGCHLD, SIG_IGN);
    fifo_open_write();
    RestaurantState* state = get_state();

    while (true) {
        // losowy odstęp czasowy między przyjściem grup
        sleep(rand() % 3);

        handle_create_group();

        // Opcjonalnie log lub inne czynności w przerwie między generowaniem
    }

    fifo_close_write();
}
