#include "ipc_manager.h"
#include "client.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

/* ---------- DEFINICJE GLOBALI ---------- */

key_t SHM_KEY = -1;
key_t SEM_KEY = -1;
key_t MSG_KEY = -1;

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;

int client_qid = -1;
int service_qid = -1;

RestaurantState* state = nullptr;

int fifo_fd_write = -1;
int fifo_fd_read = -1;

/* ---------- MESSAGE QUEUES ---------- */

int create_queue(char proj_id) {
    key_t key = ftok(".", proj_id);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, IPC_CREAT | 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget failed");

    return qid;
}

int connect_queue(char proj_id) {
    key_t key = ftok(".", proj_id);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget connect failed");

    return qid;
}


void remove_queue(char proj_id) {
    CHECK_ERR(msgctl(proj_id, IPC_RMID, nullptr), ERR_IPC_MSG, "msgctl remove failed");
}

void queue_send_request(const ServiceRequest& msg) {
    CHECK_ERR(
        msgsnd(service_qid, &msg, sizeof(ServiceRequest) - sizeof(long), 0),
        ERR_IPC_MSG,
        "msgsnd request failed"
    );
}

void queue_recv_request(ServiceRequest& msg, long mtype) {
    CHECK_ERR(
        msgrcv(service_qid, &msg, sizeof(ServiceRequest) - sizeof(long), mtype, 0),
        ERR_IPC_MSG,
        "msgrcv request faileddd"
    );
}

void queue_send_request(const ClientRequest& msg) {
    CHECK_ERR(
        msgsnd(client_qid, &msg, sizeof(ClientRequest) - sizeof(long), 0),
        ERR_IPC_MSG,
        "msgsnd request failed"
    );
}

void queue_recv_request(ClientRequest& msg, long mtype) {
    CHECK_ERR(
        msgrcv(client_qid, &msg, sizeof(ClientRequest) - sizeof(long), mtype, 0),
        ERR_IPC_MSG,
        "msgrcv request failed"
    );
}

void queue_send_response(const ClientResponse& msg) {
    CHECK_ERR(
        msgsnd(client_qid, &msg, sizeof(ClientResponse) - sizeof(long), 0),
        ERR_IPC_MSG,
        "msgsnd response failed"
    );
}

void queue_recv_response(ClientResponse& msg, long mtype) {
    CHECK_ERR(
        msgrcv(client_qid, &msg, sizeof(ClientResponse) - sizeof(long), mtype, 0),
        ERR_IPC_MSG,
        "msgrcv response failed"
    );
}

/* ---------- FIFO LOGGER ---------- */

void fifo_init() {
    if (access(FIFO_PATH, F_OK) == -1) {
        if (mkfifo(FIFO_PATH, 0600) == -1) {
            CHECK_ERR(-1, ERR_IPC_INIT, "mkfifo");
        }
    }
}

void fifo_init_close_signal() {
    mkfifo(CLOSE_FIFO, 0600); // ignoruj błąd, jeśli istnieje
}

void fifo_open_write() {
    if (fifo_fd_write == -1) {
        fifo_fd_write = open(FIFO_PATH, O_WRONLY);
        CHECK_ERR(fifo_fd_write, ERR_IPC_INIT, "fifo open write");
    }
}

void fifo_close_write() {
    if (fifo_fd_write != -1) {
        close(fifo_fd_write);
        fifo_fd_write = -1;
    }
}

void fifo_log(const char* msg) {
    P(SEM_MUTEX_LOGS);
    CHECK_ERR(fifo_fd_write, ERR_IPC_INIT, "fifo open write");
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "%s\n", msg);
    if (write(fifo_fd_write, buffer, len) == -1) {
        fprintf(stderr, "FIFO write error: %s\n", strerror(errno));
    }
    V(SEM_MUTEX_LOGS);
}

void logger_loop(const char* filename) {
    fifo_fd_read = open(FIFO_PATH, O_RDONLY);
    CHECK_ERR(fifo_fd_read, ERR_IPC_INIT, "fifo open read");

    FILE* file = fopen(filename, "w");
    CHECK_NULL(file, ERR_FILE_IO, "fopen log file");

    char buffer[512];
    ssize_t n;

    while ((n = read(fifo_fd_read, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        fprintf(file, "%s", buffer);
        fflush(file);
    }

    fclose(file);
    close(fifo_fd_read);
    fifo_fd_read = -1;
}

/* ---------- SIGNAL HANDLER ---------- */

void handle_manager_signal(int sig) {
    RestaurantState* s = get_state();
    if (!s) return;

    switch (sig) {
    case SIGUSR1: s->sig_accelerate = 1; break;
    case SIGUSR2: s->sig_slowdown = 1; break;
    case SIGTERM: s->sig_evacuate = 1; break;
    default: break;
    }
}

/* ---------- SEMAPHORES ---------- */

static void sem_set(int semnum, int val) {
    union semun {
        int val;
        struct semid_ds* buf;
        unsigned short* array;
    } arg;
    arg.val = val;
    CHECK_ERR(semctl(sem_id, semnum, SETVAL, arg), ERR_SEM_OP, "semctl SETVAL");
}

void P(int semnum) {
    struct sembuf op = { (unsigned short)semnum, -1, 0 };
    CHECK_ERR(semop(sem_id, &op, 1), ERR_SEM_OP, "semop P");
}

void V(int semnum) {
    struct sembuf op = { (unsigned short)semnum, 1, 0 };
    CHECK_ERR(semop(sem_id, &op, 1), ERR_SEM_OP, "semop V");
}

/* ---------- QUEUES (VIP / NORMAL) ---------- */

bool queue_push(pid_t groupPid, bool vipStatus) {
    bool success = false;

    P(SEM_MUTEX_QUEUE);

    if (vipStatus) {
        if (state->vipQueue.count < MAX_QUEUE) {
            state->vipQueue.groupPid[state->vipQueue.count++] = groupPid;
            success = true;
        }
    }
    else {
        if (state->normalQueue.count < MAX_QUEUE) {
            state->normalQueue.groupPid[state->normalQueue.count++] = groupPid;
            success = true;
        }
    }

    V(SEM_MUTEX_QUEUE);

    if (success) {
        if (vipStatus)
            V(SEM_QUEUE_USED_VIP);
        else
            V(SEM_QUEUE_USED_NORMAL);
    }

    return success;
}


/*int queue_pop(int requiredSize, bool vipSuitable) {
    int groupID = -1;

    P(SEM_MUTEX_QUEUE);

    int* q = vipSuitable ? state->vipQueue.groupID : state->normalQueue.groupID;
    int* count = vipSuitable ? &state->vipQueue.count : &state->normalQueue.count;

    if (*count > 0) {
        groupID = q[0];
        for (int i = 1; i < *count; i++) q[i - 1] = q[i];
        (*count)--;

        if (vipSuitable) V(SEM_QUEUE_FREE_VIP);
        else V(SEM_QUEUE_FREE_NORMAL);
    }

    V(SEM_MUTEX_QUEUE);

    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "QUEUE: pop returning groupID=%d", groupID);
    fifo_log(buffer);

    return groupID;
}*/

/* ---------- SHM / SEM / MSG INIT ---------- */

int ipc_init() {
    SHM_KEY = ftok(".", 'A'); CHECK_ERR(SHM_KEY, ERR_IPC_INIT, "ftok SHM");
    SEM_KEY = ftok(".", 'B'); CHECK_ERR(SEM_KEY, ERR_IPC_INIT, "ftok SEM");

    shm_id = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0600);
    CHECK_ERR(shm_id, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shm_id, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    sem_id = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0600);
    CHECK_ERR(sem_id, ERR_IPC_INIT, "semget");

    sem_set(SEM_MUTEX_STATE, 1);
    sem_set(SEM_MUTEX_QUEUE, 1);
    sem_set(SEM_MUTEX_LOGS, 1);
    sem_set(SEM_MUTEX_BELT, 1);

    sem_set(SEM_BELT_SLOTS, BELT_SIZE);
    sem_set(SEM_BELT_ITEMS, 0);
    sem_set(SEM_TABLES, TABLE_COUNT);
    sem_set(SEM_QUEUE_FREE_VIP, MAX_QUEUE);
    sem_set(SEM_QUEUE_FREE_NORMAL, MAX_QUEUE);
    sem_set(SEM_QUEUE_USED_VIP, 0);
    sem_set(SEM_QUEUE_USED_NORMAL, 0);

    client_qid = create_queue(CLIENT_REQ_QUEUE);
    service_qid = create_queue(SERVICE_REQ_QUEUE);

    fifo_init();
    fifo_init_close_signal();
    memset(state, 0, sizeof(RestaurantState));

    return 0;
}

RestaurantState* get_state() {
    return state;
}

/* ---------- CLEANUP ---------- */

void ipc_cleanup() {
    if (state && state != (void*)-1) { shmdt(state); state = NULL; }
    if (shm_id != -1) { shmctl(shm_id, IPC_RMID, NULL); shm_id = -1; }
    if (sem_id != -1) { semctl(sem_id, 0, IPC_RMID); sem_id = -1; }
    if (msg_id != -1) { msgctl(msg_id, IPC_RMID, NULL); msg_id = -1; }
    if (client_qid != -1) msgctl(client_qid, IPC_RMID, nullptr);
    if (service_qid != -1) msgctl(service_qid, IPC_RMID, nullptr);

    unlink(FIFO_PATH);
    unlink(CLOSE_FIFO);
}