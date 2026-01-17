#include "service.h"

int assign_table(RestaurantState* state, bool vipStatus, int groupSize, int groupID) {
    for (int i = 0; i < TABLE_COUNT; i++) {
        P(SEM_MUTEX_STATE);

        Table* t = &state->tables[i];

        // VIP restriction – tylko stoliki jednoosobowe są zabronione
        if (vipStatus && t->capacity == 1) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        int freeSeats = t->capacity - t->occupiedSeats;

        // CASE A: pusty stolik
        if (t->occupiedSeats == 0 && freeSeats >= groupSize) {
            t->slotGroupID[0] = groupID;
            t->slotGroupID[1] = -1;

            t->occupiedSeats = groupSize;
            state->currentGuestCount += groupSize;
            if (vipStatus)
                state->currentVIPCount++;

            V(SEM_MUTEX_STATE);
            return i;
        }

        // CASE B: jeden slot zajęty
        if (t->slotGroupID[1] < 0 && freeSeats >= groupSize) {
            int firstGroupID = t->slotGroupID[0];

            if (t->occupiedSeats == groupSize) {
                t->slotGroupID[1] = groupSize;
                t->occupiedSeats += groupSize;

                state->currentGuestCount += groupSize;
                if (vipStatus)
                    state->currentVIPCount++;

                V(SEM_MUTEX_STATE);
                return i;
            }
        }

        V(SEM_MUTEX_STATE);
    }

    return -1;
}

void handle_assign_group(RestaurantState* state, pid_t groupPid) {
    char logBuffer[256];
    time_t timestamp;

    // ===== 1. zapytanie clients o dane grupy (po pid) =====
    ServiceRequest req{};
    req.mtype = groupPid;
    req.type = REQ_GET_GROUP;
    req.extraData = 0;

    queue_send_request(req);

    ClientResponse resp{};
    queue_recv_response(resp);

    // ===== 2. sprawdzenie miejsca w lokalu =====
    P(SEM_MUTEX_STATE);
    int freeSeatsTotal = TOTAL_SEATS - state->currentGuestCount;
    V(SEM_MUTEX_STATE);

    if (freeSeatsTotal < resp.groupSize) {
        ServiceRequest reject{};
        reject.mtype = groupPid;
        reject.type = REQ_GROUP_REJECT;

        queue_send_request(reject);

        timestamp = time(NULL);
        snprintf(logBuffer, sizeof(logBuffer),
            "[%ld] [GROUP REJECTED] pid=%d groupID=%d size=%d reason=NO_SPACE_IN_RESTAURANT",
            timestamp, groupPid, resp.groupID, resp.groupSize
        );
        fifo_log(logBuffer);
        return;
    }

    // ===== 3. próba przydziału stolika =====
    int assignedTable = assign_table(state, resp.vipStatus, resp.groupSize, resp.groupID);

    if (assignedTable) {
        snprintf(logBuffer, sizeof(logBuffer),
            "[%ld] [TABLE ASSIGNED] table=%d pid=%d groupID=%d size=%d vip=%d",
            timestamp, assignedTable, groupPid, resp.groupID, resp.groupSize, resp.vipStatus
        );
        fifo_log(logBuffer);
        return;
    }
    

    // ===== 4. brak stolika → kolejka =====
    bool queued = queue_push(groupPid, resp.vipStatus);

    if (queued) {
        timestamp = time(NULL);
        snprintf(logBuffer, sizeof(logBuffer),
            "[%ld] [GROUP QUEUED] pid=%d groupID=%d size=%d vip=%d",
            timestamp, groupPid, resp.groupID, resp.groupSize, resp.vipStatus
        );
        fifo_log(logBuffer);
        return;
    }

    // ===== 5. brak miejsca w kolejce =====
    ServiceRequest reject{};
    reject.mtype = groupPid;
    reject.type = REQ_GROUP_REJECT;
    reject.extraData = 0;

    queue_send_request(reject);
}

/*void free_table(RestaurantState* state, int groupID) {
    IPCRequestMessage req{};
    req.mtype = 1;
    req.req.type = REQ_GET_GROUP_INFO;
    req.req.groupID = groupID;

    ipc_send_request(req);

    IPCResponseMessage resp{};
    ipc_recv_response(resp);
    if (resp.resp.status != 0) return;

    P(SEM_MUTEX_STATE);
    state->currentGuestCount -= resp.resp.groupSize;
    if (resp.resp.vipStatus) state->currentVIPCount--;
    V(SEM_MUTEX_STATE);

    V(SEM_TABLES);
}*/

void start_service() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    fifo_open_write();

    req_qid = connect_queue(REQ_QUEUE_PROJ);
    resp_qid = connect_queue(RESP_QUEUE_PROJ);

    RestaurantState* state = get_state();

    while (true) {
        // Blokujące odbieranie komunikatu od grupy
        ClientRequest req{};
        queue_recv_request(req);  // blokuje, dopóki nie przyjdzie komunikat

        char logBuffer[256];
        time_t timestamp = time(NULL);

        // Logowanie przychodzącego żądania
        snprintf(logBuffer, sizeof(logBuffer),
            "[%ld] [SERVICE RECV] groupID=%d, type=%d, extraData[pid]=%d",
            timestamp, req.groupID, req.type, req.extraData);
        fifo_log(logBuffer);

        // Obsługa komunikatu wg typu
        switch (req.type) {
        case REQ_ASSIGN_GROUP:
            handle_assign_group(state, req.extraData);
            break;
        case REQ_CONSUME_DISH:
            //handle_service_consume_dish(req, state);
            break;
        case REQ_GROUP_DONE:
            //handle_service_group_done(req, state);
            break;
        default:
            // nieznany typ, ignorujemy lub logujemy
            break;
        }

        //assign_tables(state);  // przydzielanie stolików
    }

    fifo_close_write();
}




static int check_close_signal() {
    char buf[32];
    int fd = open(CLOSE_FIFO, O_RDONLY | O_NONBLOCK);
    if (fd == -1) return 0;
    int ret = read(fd, buf, sizeof(buf));
    close(fd);
    if (ret > 0 && strcmp(buf, "CLOSE_RESTAURANT") == 0)
        return 1;
    return 0;
}