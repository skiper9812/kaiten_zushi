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

void handle_consume_dish(Group& g, int dishCount) {
    char logBuffer[256];
    time_t timestamp = time(NULL);

    // Tworzymy komunikat do service
    ClientRequest req{};
    req.mtype = CONSUME;                  // typ wiadomości w kolejce
    req.type = REQ_CONSUME_DISH;    // żądanie konsumpcji
    req.groupID = g.getGroupID();
    req.extraData = dishCount;      // liczba dań do zjedzenia w tym kroku

    queue_send_request(req);         // wysyłamy do service

    // Logujemy chęć konsumpcji
    snprintf(logBuffer, sizeof(logBuffer),
        "[%ld] [REQUEST CONSUME] groupID=%d, pid=%d, dishesRequested=%d",
        timestamp, g.getGroupID(), getpid(), dishCount);
    fifo_log(logBuffer);
}


void handle_group_done(const Group& g) {
    char logBuffer[256];
    time_t timestamp = time(NULL);

    // Tworzymy komunikat do service
    ClientRequest req{};
    req.mtype = DONE;                  // typ wiadomości w kolejce
    req.type = REQ_GROUP_DONE;      // żądanie zakończenia
    req.groupID = g.getGroupID();
    req.extraData = 0;              // dodatkowo można przesłać PID lub inne info

    queue_send_request(req);         // wysyłamy do service

    // Logujemy intencję zakończenia
    snprintf(logBuffer, sizeof(logBuffer),
        "[%ld] [REQUEST DONE] groupID=%d, pid=%d",
        timestamp, g.getGroupID(), getpid());
    fifo_log(logBuffer);
}

static void handle_get_group(const Group& g) {
    ClientResponse resp{};
    time_t timestamp = time(NULL);

    resp.mtype = 1;
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
        "[%ld] [GROUP REJECTED] pid=%d groupID=%d size=%d vip=%d dishes=%d",
        timestamp, getpid(), g.getGroupID(), g.getGroupSize(), g.getVipStatus(), g.getDishesToEat()
    );

    fifo_log(logBuffer);

    _exit(0);
}

// ===== statyczne pola Group =====
int Group::nextGroupID = 0;

void group_loop(Group& g) {
    req_qid = connect_queue(REQ_QUEUE_PROJ);
    resp_qid = connect_queue(RESP_QUEUE_PROJ);

    char logBuffer[256];
    time_t timestamp = time(NULL);

    // ===== Powiadomienie service.cpp, że nowa grupa przyszła =====
    ClientRequest request{};
    request.mtype = ASSIGN;          // typ wiadomości w kolejce (dowolnie ustalony)
    request.type = REQ_ASSIGN_GROUP;         // typ żądania CREATE_GROUP
    request.groupID = g.getGroupID();
    request.extraData = getpid();            // PID procesu grupy

    queue_send_request(request);             // wysyłamy do service

    // Logowanie powstania grupy
    snprintf(logBuffer, sizeof(logBuffer),
        "[%ld] [GROUP CREATED] groupID=%d, pid=%d, groupSize=%d, dishesLeft=%d, vipStatus=%d",
        timestamp, g.getGroupID(), getpid(), g.getGroupSize(), g.getDishesToEat(), g.getVipStatus());
    fifo_log(logBuffer);
    // =================================================================

    // ===== Główna pętla symulacji grupy =====
    while (!g.isFinished()) {
        // tutaj decyzje grupy: consumeDish(), groupDone(), etc.
        ServiceRequest req{};
        queue_recv_request(req);   // odbiera request od service

        switch (req.type) {
        case REQ_GET_GROUP:
            handle_get_group(g);
            break;
        case REQ_GROUP_REJECT:
            handle_reject_group(g);
            break;
        default:
            break;
        }

        sleep(1);  // symulacja czasu między decyzjami
    }
}

// =====================================================
// GŁÓWNA PĘTLA CLIENT
// =====================================================

void start_clients() {
    fifo_open_write();
    RestaurantState* state = get_state();

    while (true) {
        // losowy odstęp czasowy między przyjściem grup
        sleep(rand() % 3 + 1);  // np. 1-3 sekundy

        handle_create_group();

        // Opcjonalnie log lub inne czynności w przerwie między generowaniem
    }

    fifo_close_write();
}
