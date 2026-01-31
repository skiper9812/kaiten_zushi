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

// ============================================================================
// QUEUE HELPERS
// ============================================================================

// Creates a message queue with a specific Project ID
int createQueue(char projId) {
    key_t key = ftok(".", projId);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, IPC_CREAT | 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget failed");

    return qid;
}

// Connects to an existing message queue
int connectQueue(char projId) {
    key_t key = ftok(".", projId);
    CHECK_ERR(key, ERR_IPC_INIT, "ftok failed");

    int qid = msgget(key, 0600);
    CHECK_ERR(qid, ERR_IPC_INIT, "msgget connect failed");

    return qid;
}

// Removes a message queue
void removeQueue(char projId) {
    CHECK_ERR(msgctl(projId, IPC_RMID, nullptr), ERR_IPC_MSG, "msgctl remove failed");
}

template<typename T>
struct QueueRecvTraits {
    static constexpr int flags = 0;
};

// Premium (Chef) queue should be non-blocking by default (polling behavior)
template<>
struct QueueRecvTraits<PremiumRequest> {
    static constexpr int flags = IPC_NOWAIT;
};

// Generic safe queue send with semaphore rate limiting
// Handles interrupts (EINTR) and termination signaling
template<typename T>
void queueSend(int qid, int semFree, int semItems,
    const T& msg, ErrorCode err, const char* errMsg)
{
    // Wait for free space in queue
    P(semFree);
    
    if (terminate_flag || evacuate_flag) {
        V(semFree); 
        return;
    }

    for (;;) {
        if (terminate_flag || evacuate_flag) {
            V(semFree);
            return;
        }
        
        int ret = msgsnd(qid, &msg, sizeof(T) - sizeof(long), 0);
        if (ret == 0) {
            return; 
        }
        
        int savedErrno = errno;
        if (savedErrno == EAGAIN) {
            SIM_SLEEP(10000);
            continue;
        }
        if (savedErrno == EINTR) {
            continue; 
        }
        
        if (terminate_flag || evacuate_flag) {
            V(semFree);
            return;
        }
        handleError(err, errMsg, savedErrno);
        return;
    }
}

// Generic safe queue receive
// Handles timeouts, interrupts, and termination signaling
template<typename T>
bool queueRecv(int qid, int semItems, int semFree,
    T& msg, long mtype,
    ErrorCode err, const char* errMsg)
{
    constexpr int baseFlags = QueueRecvTraits<T>::flags;

    for (;;) {
        if (terminate_flag || evacuate_flag)
            return false;
        
        ssize_t ret = msgrcv(qid, &msg, sizeof(T) - sizeof(long), mtype, baseFlags);

        if (ret >= 0) {
            V(semFree); // Signal free space
            return true;
        }

        int savedErrno = errno;
        
        if (savedErrno == EINTR) {
            continue;
        }
        
        if (savedErrno == ENOMSG || savedErrno == EAGAIN) {
            if (baseFlags & IPC_NOWAIT) {
                return false;
            }
            continue; 
        }

        ErrorDecision d = handleError(err, errMsg, savedErrno);

        if (d == ERR_DECISION_RETRY)
            continue;

        return false;
    }
}

// --- Type-safe Wrappers ---

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

// ============================================================================
// FIFO LOGGING
// ============================================================================

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
    if (fifoFdWrite == -1) return; 
    
    P(SEM_MUTEX_LOGS);
    
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "%s\n", msg);
    
    ssize_t written = 0;
    while (written < len) {
        ssize_t ret = write(fifoFdWrite, buffer + written, len - written);
        if (ret == -1) {
            if (errno == EINTR) continue; 
            break;
        }
        written += ret;
    }

    V(SEM_MUTEX_LOGS);
}

// Dedicated logging process loop
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

// ============================================================================
// SEMAPHORES
// ============================================================================

static void semSet(int semnum, int val) {
    union semun {
        int val;
        struct semid_ds* buf;
        unsigned short* array;
    } arg;
    arg.val = val;
    CHECK_ERR(semctl(semId, semnum, SETVAL, arg), ERR_SEM_OP, "semctl SETVAL");
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Wait (Decrement) operation with timeout to allow flag checking
void P(int semnum) {
    struct sembuf op = { (unsigned short)semnum, -1, 0 };
    struct timespec ts;

    for (;;) {
        if (terminate_flag || evacuate_flag)
            return;

        ts.tv_sec = 0;
        ts.tv_nsec = 500000000; // 500ms

        int ret = semtimedop(semId, &op, 1, &ts);
        
        if (ret != -1)
            return;

        int savedErrno = errno;
        
        if (savedErrno == EAGAIN) {
             continue; // Timeout
        }

        if (savedErrno == EINTR || savedErrno == EIDRM) {
            if (terminate_flag || evacuate_flag)
                return;
            continue;
        }

        ErrorDecision d = handleError(ERR_SEM_OP, "semtimedop P", savedErrno);

        if (d == ERR_DECISION_IGNORE)
            return;
        if (d == ERR_DECISION_FATAL)
            exit(EXIT_FAILURE);
    }
}

// Signal (Increment) operation
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

int getSemValue(int semnum) {
    int val = semctl(semId, semnum, GETVAL);
    if (val == -1) return -1;
    return val;
}

// Used to push group into local process memory queues (VIP/Normal)
bool queuePush(pid_t groupPid, bool vipStatus, int groupSize, int groupID) {

    if (terminate_flag || evacuate_flag)
        return false;

    P(SEM_MUTEX_QUEUE);

    if (vipStatus) {
        if (state->vipQueue.count >= MAX_QUEUE) {
           V(SEM_MUTEX_QUEUE);
           return false; 
        }
        state->vipQueue.groupPid[state->vipQueue.count] = groupPid;
        state->vipQueue.groupSize[state->vipQueue.count] = groupSize;
        state->vipQueue.groupID[state->vipQueue.count] = groupID;
        state->vipQueue.count++;
    } else {
        if (state->normalQueue.count >= MAX_QUEUE) {
           V(SEM_MUTEX_QUEUE);
           return false; 
        }
        state->normalQueue.groupPid[state->normalQueue.count] = groupPid;
        state->normalQueue.groupSize[state->normalQueue.count] = groupSize;
        state->normalQueue.groupID[state->normalQueue.count] = groupID;
        state->normalQueue.count++;
    }

    V(SEM_MUTEX_QUEUE);
    
    return true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

int ipcInit() {
    SHM_KEY = ftok(".", 'A'); CHECK_ERR(SHM_KEY, ERR_IPC_INIT, "ftok SHM");
    SEM_KEY = ftok(".", 'B'); CHECK_ERR(SEM_KEY, ERR_IPC_INIT, "ftok SEM");

    shmId = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0600);
    CHECK_ERR(shmId, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shmId, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    semId = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0600);
    CHECK_ERR(semId, ERR_IPC_INIT, "semget");

    // Initialize Semaphores
    semSet(SEM_MUTEX_STATE, 1);
    semSet(SEM_MUTEX_QUEUE, 1);
    semSet(SEM_MUTEX_LOGS, 1);
    semSet(SEM_MUTEX_BELT, 1);

    semSet(SEM_BELT_SLOTS, BELT_SIZE);
    semSet(SEM_BELT_ITEMS, 0);

    semSet(SEM_TABLES, TABLE_COUNT);

    semSet(SEM_QUEUE_FREE_VIP, MAX_QUEUE);
    semSet(SEM_QUEUE_FREE_NORMAL, MAX_QUEUE);
    semSet(SEM_QUEUE_USED_VIP, 0);
    semSet(SEM_QUEUE_USED_NORMAL, 0);

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
    state->totalGroupsCreated = 0;
    state->startTime = time(NULL);
    state->totalPauseNanoseconds = 0;

    return 0;
}

RestaurantState* getState() {
    return state;
}

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

// ============================================================================
// PAUSE MONITOR
// ============================================================================

static void monitorSigcontHandler(int sig) {
}

static void* pauseMonitorThread(void* arg) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCONT);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    struct sigaction sa = { 0 };

    sa.sa_handler = monitorSigcontHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCONT, &sa, NULL); 

    struct timespec t1, t2;

    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        pause(); // Wait for SIGCONT/signal
        
        clock_gettime(CLOCK_MONOTONIC, &t2);
        
        long long ns = (t2.tv_sec - t1.tv_sec) * 1000000000LL + (t2.tv_nsec - t1.tv_nsec);
        
        // Filter out small wakeups, accumulate real pauses
        if (ns > 1000000)
            if (state) state->totalPauseNanoseconds += ns;
    }
    return nullptr;
}

void startPauseMonitor() {
    // Block SIGCONT in main thread so it's inherited blocked by future threads
    sigset_t sigcontSet;
    sigemptyset(&sigcontSet);
    sigaddset(&sigcontSet, SIGCONT);
    pthread_sigmask(SIG_BLOCK, &sigcontSet, NULL);

    pthread_t monitorThread;
    if (pthread_create(&monitorThread, NULL, pauseMonitorThread, NULL) != 0) {
        perror("Failed to create monitor thread");
    }
}
