#include "service.h"

int assign_table(RestaurantState* state, bool vipStatus, int groupSize, int groupID, pid_t pid)
{
    for (int i = 0; i < TABLE_COUNT; ++i) {
        P(SEM_MUTEX_STATE);
        Table& t = state->tables[i];

        // VIP: zakaz tylko dla stolików 1-os.
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

        // sprawdzenie istniejących grup
        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid == -1)
                continue;

            if (referenceSize == -1)
                referenceSize = t.slots[s].size;
            else if (t.slots[s].size != referenceSize) {
                compatible = false;
                break;
            }
        }

        // jeżeli stolik nie jest pusty → musi być zgodność liczebności
        if (referenceSize != -1 && referenceSize != groupSize)
            compatible = false;

        if (!compatible) {
            V(SEM_MUTEX_STATE);
            continue;
        }

        // szukamy wolnego slotu
        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != -1)
                continue;

            t.slots[s].pid = pid;
            t.slots[s].size = groupSize;
            t.slots[s].vipStatus = vipStatus;

            t.occupiedSeats += groupSize;
            state->currentGuestCount += groupSize;
            if (vipStatus)
                state->currentVIPCount++;

            V(SEM_MUTEX_STATE);
            return i;
        }

        V(SEM_MUTEX_STATE);
    }

    return -1;
}

void handle_assign_group(RestaurantState* state, pid_t groupPid) {
    char logBuffer[256];
    time_t* timestamp;

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
    int freeSeatsTotal = TOTAL_SEATS - state->currentGuestCount + SEM_QUEUE_FREE_NORMAL + SEM_QUEUE_FREE_VIP;
    V(SEM_MUTEX_STATE);

    if (freeSeatsTotal < resp.groupSize) {
        ServiceRequest reject{};
        reject.mtype = groupPid;
        reject.type = REQ_GROUP_REJECT;

        queue_send_request(reject);

        time_t timestamp = time(NULL);
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE] GROUP REJECTED | pid=%d groupID=%d size=%d reason=NO_SPACE_IN_RESTAURANT\033[0m",
            timestamp, resp.pid, resp.groupID, resp.groupSize
        );
        fifo_log(logBuffer);
        return;
    }

    // ===== 3. próba przydziału stolika =====
    int assignedTable = assign_table(state, resp.vipStatus, resp.groupSize, resp.groupID, resp.pid);

    if (assignedTable != -1) {
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [TABLE ASSIGNED] table=%d pid=%d groupID=%d size=%d vip=%d\033[0m",
            time(NULL), assignedTable, resp.pid, resp.groupID, resp.groupSize, resp.vipStatus
        );
        fifo_log(logBuffer);

        ServiceRequest req{};
        req.mtype = resp.pid;
        req.type = REQ_GROUP_ASSIGNED;
        req.extraData = assignedTable;

        queue_send_request(req);
        return;
    }
    

    // ===== 4. brak stolika → kolejka =====
    bool queued = queue_push(groupPid, resp.vipStatus);

    if (queued) {
        time_t timestamp = time(NULL);
        snprintf(logBuffer, sizeof(logBuffer),
            "\033[32m[%ld] [SERVICE] GROUP QUEUED | pid=%d groupID=%d size=%d vip=%d\033[0m",
            timestamp, resp.pid, resp.groupID, resp.groupSize, resp.vipStatus
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

void handle_service_group_done(RestaurantState* state, pid_t pid, int* eatenCount)
{
    P(SEM_MUTEX_STATE);

    for (int i = 0; i < TABLE_COUNT; ++i) {
        Table& t = state->tables[i];

        for (int s = 0; s < MAX_TABLE_SLOTS; ++s) {
            if (t.slots[s].pid != pid)
                continue;

            int size = t.slots[s].size;
            bool vip = t.slots[s].vipStatus;

            t.slots[s].pid = -1;
            t.slots[s].size = 0;
            t.slots[s].vipStatus = false;

            for (int i = 0; i < COLOR_COUNT; ++i) {
                if (eatenCount[i] == 0) continue;

                colors c = colorFromIndex(i);
                int price = priceForColor(c);
                int total_price = 

                state->soldCount[i] += eatenCount[i];
                state->soldValue[i] += eatenCount[i] * price;
                state->revenue += state->soldValue[i];
            }

            t.occupiedSeats -= size;
            state->currentGuestCount -= size;
            if (vip)
                state->currentVIPCount--;

            // jeżeli stolik całkowicie pusty → zwalniamy zasób
            if (t.occupiedSeats == 0)
                V(SEM_TABLES);

            V(SEM_MUTEX_STATE);
            return;
        }
    }

    V(SEM_MUTEX_STATE);
}


void start_service() {
    signal(SIGUSR1, handle_manager_signal);
    signal(SIGUSR2, handle_manager_signal);
    signal(SIGTERM, handle_manager_signal);

    fifo_open_write();

    client_qid = connect_queue(CLIENT_REQ_QUEUE);
    service_qid = connect_queue(SERVICE_REQ_QUEUE);

    RestaurantState* state = get_state();

    while (true) {
        // Blokujące odbieranie komunikatu od grupy
        ClientRequest req{};
        queue_recv_request(req);  // blokuje, dopóki nie przyjdzie komunikat

        char logBuffer[256];
        time_t timestamp = time(NULL);

        // Obsługa komunikatu wg typu
        switch (req.type) {
        case REQ_ASSIGN_GROUP:
            handle_assign_group(state, req.pid);
            break;
        case REQ_GROUP_DONE:
            handle_service_group_done(state, req.pid, req.eatenCount);
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