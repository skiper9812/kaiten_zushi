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

/* ---------- GLOBAL DEFINITIONS ---------- */

key_t SHM_KEY = -1;
key_t SEM_KEY = -1;
key_t MSG_KEY = -1;

int shmId = -1;
int semId = -1;
int msgId = -1;

int clientQid = -1;
int serviceQid = -1;
int premiumQid = -1;

RestaurantState* state = nullptr;

int fifoFdWrite = -1;
int fifoFdRead = -1;

/* ---------- MESSAGE QUEUES ---------- */

int createQueue(char projId) {
    key_t key = ftok(".", projId);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, IPC_CREAT | 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget failed");

    return qid;
}

int connectQueue(char projId) {
    key_t key = ftok(".", projId);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget connect failed");

    return qid;
}

void removeQueue(char projId) {
    CHECK_ERR(msgctl(projId, IPC_RMID, nullptr), ERR_IPC_MSG, "msgctl remove failed");
}

template<typename T>
struct QueueRecvTraits {
    static constexpr int flags = 0;
};

template<>
struct QueueRecvTraits<PremiumRequest> {
    static constexpr int flags = IPC_NOWAIT;
};

template<typename T>
void queueSend(int qid, int semFree, int semItems,
    const T& msg, ErrorCode err, const char* errMsg)
{
    P(semFree);

    ErrorDecision d = CHECK_ERR(
        msgsnd(qid, &msg, sizeof(T) - sizeof(long), 0),
        err,
        errMsg
    );

    if (d == ERR_DECISION_IGNORE) {
        return;
    }

    if (d == ERR_DECISION_FATAL)
        exit(EXIT_FAILURE);
}

template<typename T>
bool queueRecv(int qid, int semItems, int semFree,
    T& msg, long mtype,
    ErrorCode err, const char* errMsg)
{
    constexpr int flags = QueueRecvTraits<T>::flags;

    for (;;) {
        ssize_t ret = msgrcv(qid, &msg, sizeof(T) - sizeof(long), mtype, flags);

        if (ret >= 0) {
            V(semFree);
            return true;
        }

        if (errno == EINTR) {
            if (terminate_flag || evacuate_flag)
                return false;
            continue;
        }

        if constexpr (flags & IPC_NOWAIT) {
            if (errno == ENOMSG) {
                return false;
            }
        }

        ErrorDecision d = handleError(err, errMsg, errno);

        if (d == ERR_DECISION_RETRY)
            continue;

        return false;
    }
}

void queueSendRequest(const ServiceRequest& msg) {
    queueSend(serviceQid,
        SEM_SERVICE_FREE,
        SEM_SERVICE_ITEMS,
        msg,
        ERR_IPC_MSG,
        "msgsnd ServiceRequest failed");
}

void queueRecvRequest(ServiceRequest& msg, long mtype) {
    queueRecv(serviceQid,
        SEM_SERVICE_ITEMS,
        SEM_SERVICE_FREE,
        msg,
        mtype,
        ERR_IPC_MSG,
        "msgrcv ServiceRequest failed");
}

void queueSendRequest(const PremiumRequest& msg) {
    queueSend(premiumQid,
        SEM_PREMIUM_FREE,
        SEM_PREMIUM_ITEMS,
        msg,
        ERR_IPC_MSG,
        "msgsnd PremiumRequest failed");
}

bool queueRecvRequest(PremiumRequest& msg, long mtype) {
    return queueRecv(premiumQid,
        SEM_PREMIUM_ITEMS,
        SEM_PREMIUM_FREE,
        msg,
        mtype,
        ERR_IPC_MSG,
        "msgrcv PremiumRequest failed");
}

void queueSendRequest(const ClientRequest& msg) {
    queueSend(clientQid,
        SEM_CLIENT_FREE,
        SEM_CLIENT_ITEMS,
        msg,
        ERR_IPC_MSG,
        "msgsnd ClientRequest failed");
}

void queueRecvRequest(ClientRequest& msg, long mtype) {
    queueRecv(clientQid,
        SEM_CLIENT_ITEMS,
        SEM_CLIENT_FREE,
        msg,
        mtype,
        ERR_IPC_MSG,
        "msgrcv ClientRequest failed");
}

void queueSendResponse(const ClientResponse& msg) {
    queueSend(clientQid,
        SEM_CLIENT_FREE,
        SEM_CLIENT_ITEMS,
        msg,
        ERR_IPC_MSG,
        "msgsnd ClientResponse failed");
}

void queueRecvResponse(ClientResponse& msg, long mtype) {
    queueRecv(clientQid,
        SEM_CLIENT_ITEMS,
        SEM_CLIENT_FREE,
        msg,
        mtype,
        ERR_IPC_MSG,
        "msgrcv ClientResponse failed");
}

/* ---------- FIFO LOGGER ---------- */

void fifoInit() {
    if (access(FIFO_PATH, F_OK) == -1) {
        if (mkfifo(FIFO_PATH, 0600) == -1) {
            CHECK_ERR(-1, ERR_IPC_INIT, "mkfifo");
        }
    }
}

void fifoInitCloseSignal() {
    mkfifo(CLOSE_FIFO, 0600);
}

void fifoOpenWrite() {
    if (fifoFdWrite == -1) {
        fifoFdWrite = open(FIFO_PATH, O_WRONLY);
        CHECK_ERR(fifoFdWrite, ERR_IPC_INIT, "fifo open write");
    }
}

void fifoCloseWrite() {
    if (fifoFdWrite != -1) {
        close(fifoFdWrite);
        fifoFdWrite = -1;
    }
}

void fifoLog(const char* msg) {
    if (fifoFdWrite == -1) return;  // Silently skip if not open
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "%s\n", msg);
    
    ssize_t written = 0;
    while (written < len) {
        ssize_t ret = write(fifoFdWrite, buffer + written, len - written);
        if (ret == -1) {
            if (errno == EINTR) continue;  // Retry on interrupt
            return;  // Silently fail on other errors
        }
        written += ret;
    }
}

void loggerLoop(const char* filename) {
    fifoFdRead = open(FIFO_PATH, O_RDONLY);
    CHECK_ERR(fifoFdRead, ERR_IPC_INIT, "fifo open read");

    FILE* file = fopen(filename, "w");
    CHECK_NULL(file, ERR_FILE_IO, "fopen log file");

    char buffer[512];
    ssize_t n;

    while ((n = read(fifoFdRead, buffer, sizeof(buffer) - 1)) > 0) {
        if (n == 1 && buffer[0] == -1) break;
        buffer[n] = '\0';
        fprintf(file, "%s", buffer);
        fflush(file);
    }

    fclose(file);
    close(fifoFdRead);
    fifoFdRead = -1;
    _exit(0);
}

/* ---------- SEMAPHORES ---------- */

static void semSet(int semnum, int val) {
    union semun {
        int val;
        struct semid_ds* buf;
        unsigned short* array;
    } arg;
    arg.val = val;
    CHECK_ERR(semctl(semId, semnum, SETVAL, arg), ERR_SEM_OP, "semctl SETVAL");
}

void P(int semnum) {
    struct sembuf op = { (unsigned short)semnum, -1, IPC_NOWAIT };

    for (;;) {
        if (terminate_flag || evacuate_flag)
            return;

        int ret = semop(semId, &op, 1);
        if (ret != -1)
            return;

        int savedErrno = errno;
        
        if (savedErrno == EAGAIN) {
            SIM_SLEEP(100000);  // 100ms polling
            continue;
        }

        if (savedErrno == EINTR || savedErrno == EIDRM) {
            if (terminate_flag || evacuate_flag)
                return;
            continue;
        }

        ErrorDecision d = handleError(ERR_SEM_OP, "semop P", savedErrno);

        if (d == ERR_DECISION_IGNORE)
            return;
        if (d == ERR_DECISION_FATAL)
            exit(EXIT_FAILURE);
    }
}

void V(int semnum) {
    struct sembuf op = { (unsigned short)semnum, 1, 0 };

    for (;;) {
        ErrorDecision d = CHECK_ERR(semop(semId, &op, 1),
            ERR_SEM_OP, "semop V");

        if (d == ERR_DECISION_IGNORE)
            return;

        if (d == ERR_DECISION_RETRY)
            continue;

        if (d == ERR_DECISION_FATAL)
            exit(EXIT_FAILURE);
    }
}

/* ---------- BLOCKING QUEUE (VIP / NORMAL) ---------- */

bool queuePush(pid_t groupPid, bool vipStatus) {
    // Block until space available (blocking queue - no rejection)
    if (vipStatus)
        P(SEM_QUEUE_FREE_VIP);
    else
        P(SEM_QUEUE_FREE_NORMAL);

    if (terminate_flag || evacuate_flag)
        return false;

    P(SEM_MUTEX_QUEUE);

    if (vipStatus) {
        state->vipQueue.groupPid[state->vipQueue.count++] = groupPid;
    } else {
        state->normalQueue.groupPid[state->normalQueue.count++] = groupPid;
    }

    V(SEM_MUTEX_QUEUE);

    if (vipStatus)
        V(SEM_QUEUE_USED_VIP);
    else
        V(SEM_QUEUE_USED_NORMAL);

    return true;
}

/* ---------- SHM / SEM / MSG INIT ---------- */

int ipcInit() {
    SHM_KEY = ftok(".", 'A'); CHECK_ERR(SHM_KEY, ERR_IPC_INIT, "ftok SHM");
    SEM_KEY = ftok(".", 'B'); CHECK_ERR(SEM_KEY, ERR_IPC_INIT, "ftok SEM");

    shmId = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0600);
    CHECK_ERR(shmId, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shmId, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    semId = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0600);
    CHECK_ERR(semId, ERR_IPC_INIT, "semget");

    // Critical sections
    semSet(SEM_MUTEX_STATE, 1);
    semSet(SEM_MUTEX_QUEUE, 1);
    semSet(SEM_MUTEX_LOGS, 1);
    semSet(SEM_MUTEX_BELT, 1);

    // Belt
    semSet(SEM_BELT_SLOTS, BELT_SIZE);
    semSet(SEM_BELT_ITEMS, 0);

    // Tables
    semSet(SEM_TABLES, TABLE_COUNT);

    // Restaurant queue (blocking)
    semSet(SEM_QUEUE_FREE_VIP, MAX_QUEUE);
    semSet(SEM_QUEUE_FREE_NORMAL, MAX_QUEUE);
    semSet(SEM_QUEUE_USED_VIP, 0);
    semSet(SEM_QUEUE_USED_NORMAL, 0);

    // Message queue control
    semSet(SEM_CLIENT_FREE, CLIENT_QUEUE_SIZE);
    semSet(SEM_CLIENT_ITEMS, 0);

    semSet(SEM_SERVICE_FREE, SERVICE_QUEUE_SIZE);
    semSet(SEM_SERVICE_ITEMS, 0);

    semSet(SEM_PREMIUM_FREE, PREMIUM_QUEUE_SIZE);
    semSet(SEM_PREMIUM_ITEMS, 0);

    clientQid = createQueue(CLIENT_REQ_QUEUE);
    serviceQid = createQueue(SERVICE_REQ_QUEUE);
    premiumQid = createQueue(PREMIUM_REQ_QUEUE);

    fifoInit();
    fifoInitCloseSignal();
    memset(state, 0, sizeof(RestaurantState));
    state->startTime = time(NULL);

    return 0;
}

RestaurantState* getState() {
    return state;
}

/* ---------- CLEANUP ---------- */

void ipcCleanup() {
    if (state && state != (void*)-1) { shmdt(state); state = NULL; }
    if (shmId != -1) { shmctl(shmId, IPC_RMID, NULL); shmId = -1; }
    if (semId != -1) { semctl(semId, 0, IPC_RMID); semId = -1; }
    if (msgId != -1) { msgctl(msgId, IPC_RMID, NULL); msgId = -1; }
    if (clientQid != -1) msgctl(clientQid, IPC_RMID, nullptr);
    if (serviceQid != -1) msgctl(serviceQid, IPC_RMID, nullptr);
    if (premiumQid != -1) msgctl(premiumQid, IPC_RMID, nullptr);

    unlink(FIFO_PATH);
    unlink(CLOSE_FIFO);
}
