#include "ipc_manager.h"

key_t SHM_KEY = -1;
key_t SEM_KEY = -1;
key_t MSG_KEY = -1;

static int shm_id = -1;
static int sem_id = -1;
static int msg_id = -1;
static RestaurantState* state = NULL;

static int fifo_fd_write = -1;
static int fifo_fd_read = -1;

/* ---------- INIT FIFO ---------- */
void fifo_init() {
    if (access(FIFO_PATH, F_OK) == -1) {
        if (mkfifo(FIFO_PATH, 0600) == -1) {
            CHECK_ERR(-1, ERR_IPC_INIT, "mkfifo");
        }
    }
}

/* ---------- LOGGER LOOP ---------- */
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

/* ---------- OPEN FIFO WRITE ---------- */
void fifo_open_write() {
    if (fifo_fd_write == -1) {
        fifo_fd_write = open(FIFO_PATH, O_WRONLY);
        CHECK_ERR(fifo_fd_write, ERR_IPC_INIT, "fifo open write");
    }
}

/* ---------- CLOSE FIFO WRITE ---------- */
void fifo_close_write() {
    if (fifo_fd_write != -1) {
        close(fifo_fd_write);
        fifo_fd_write = -1;
    }
}

/* ---------- LOG FUNCTION ---------- */
void fifo_log(const char* msg) {
    //if (fifo_fd_write == -1) return;
    CHECK_ERR(fifo_fd_write, ERR_IPC_INIT, "fifo open write");

    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "%s\n", msg);
    if (write(fifo_fd_write, buffer, len) == -1) {
        fprintf(stderr, "FIFO write error: %s\n", strerror(errno));
    }
}

/* ---------- SIGNAL ---------- */

void handle_manager_signal(int sig) {
    RestaurantState* s = get_state();  // lokalny wskaŸnik
    if (!s) return;

    switch (sig) {
    case SIGUSR1:
        s->sig_accelerate = 1;
        break;
    case SIGUSR2:
        s->sig_slowdown = 1;
        break;
    case SIGTERM:
        s->sig_evacuate = 1;
        break;
    default:
        break;
    }
}

void fifo_init_close_signal() {
    // ignoruj b³¹d, jeœli FIFO ju¿ istnieje
    mkfifo(CLOSE_FIFO, 0600);
}

/* ---------- SEMAFORY ---------- */

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

/* ---------- KOMUNIKATY ---------- */
void send_premium_order(int table_id, int price) {
    struct PremiumMsg msg;
    msg.mtype = MSG_PREMIUM_TYPE;
    msg.table_id = table_id;
    msg.dish_price = price;

    CHECK_ERR(msgsnd(msg_id, &msg, sizeof(struct PremiumMsg) - sizeof(long), 0),ERR_IPC_MSG,"msgsnd premium");
}

int recv_premium_order(struct PremiumMsg* msg) {
    int ret = msgrcv(msg_id,msg,sizeof(struct PremiumMsg) - sizeof(long),MSG_PREMIUM_TYPE,0);
    CHECK_ERR(ret, ERR_IPC_MSG, "msgrcv premium");
    return ret;
}


/* ---------- IPC INIT ---------- */

int ipc_init() {
    /* KEYS */
    SHM_KEY = ftok(".", 'A');
    CHECK_ERR(SHM_KEY, ERR_IPC_INIT, "ftok SHM");

    SEM_KEY = ftok(".", 'B');
    CHECK_ERR(SEM_KEY, ERR_IPC_INIT, "ftok SEM");

    MSG_KEY = ftok(".", 'C');
    CHECK_ERR(MSG_KEY, ERR_IPC_INIT, "ftok MSG");

    /* SHM */
    shm_id = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0600);
    CHECK_ERR(shm_id, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shm_id, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    /* MSG */
    msg_id = msgget(MSG_KEY, IPC_CREAT | 0600);
    CHECK_ERR(msg_id, ERR_IPC_INIT, "msgget premium");

    /* SEM */
    sem_id = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0600);
    CHECK_ERR(sem_id, ERR_IPC_INIT, "semget");

    sem_set(SEM_MUTEX_STATE, 1);      // mutex binarny
    sem_set(SEM_MUTEX_QUEUE, 1);      // mutex binarny
    sem_set(SEM_BELT_SLOTS, BELT_SIZE);
    sem_set(SEM_BELT_ITEMS, 0);
    sem_set(SEM_TABLES, TABLE_COUNT);
    sem_set(SEM_QUEUE_FREE_VIP, MAX_QUEUE / 2);
    sem_set(SEM_QUEUE_FREE_NORMAL, MAX_QUEUE / 2);
    sem_set(SEM_QUEUE_USED_NORMAL, 0);
    sem_set(SEM_QUEUE_USED_VIP, 0);

    /* FIFO */
    fifo_init();
    fifo_init_close_signal();

    /* INIT STATE */
    memset(state, 0, sizeof(RestaurantState));

    return 0;
}

/* ---------- ACCESS ---------- */

RestaurantState* get_state() {
    return state;
}

void queue_push(Group* grp) {
    if (grp->isVip) {
        P(SEM_QUEUE_FREE_VIP);

        P(SEM_MUTEX_QUEUE);
        for (int i = state->vip_queue.count; i > 0; --i) {
            state->vip_queue.queue[i] = state->vip_queue.queue[i - 1];
        }
        state->vip_queue.queue[0] = grp;
        state->vip_queue.count++;
        V(SEM_MUTEX_QUEUE);

        V(SEM_QUEUE_USED_VIP);
    }
    else {
        P(SEM_QUEUE_FREE_NORMAL);

        P(SEM_MUTEX_QUEUE);
        for (int i = state->normal_queue.count; i > 0; --i) {
            state->normal_queue.queue[i] = state->normal_queue.queue[i - 1];
        }
        state->normal_queue.queue[0] = grp;
        state->normal_queue.count++;
        V(SEM_MUTEX_QUEUE);

        V(SEM_QUEUE_USED_NORMAL);
    }
}

static Group* pop_from_queue(GroupQueue* q, int required_size) {
    for (int i = 0; i < q->count; ++i) {
        if (q->queue[i]->groupSize <= required_size) {
            Group* grp = q->queue[i];

            for (int j = i + 1; j < q->count; ++j)
                q->queue[j - 1] = q->queue[j];

            q->count--;
            return grp;
        }
    }
    return NULL;
}

Group* queue_pop(int required_size, bool vipSuitable) {
    Group* grp = NULL;

    P(SEM_MUTEX_QUEUE);
    if (vipSuitable)
        grp = pop_from_queue(&state->vip_queue, required_size);
    else
        grp = pop_from_queue(&state->normal_queue, required_size);

    if (grp != NULL) {
        if (vipSuitable)
            V(SEM_QUEUE_FREE_VIP);
        else
            V(SEM_QUEUE_FREE_NORMAL);
    }
    V(SEM_MUTEX_QUEUE);

    return grp;
}

/* ---------- CLEANUP ---------- */

void ipc_cleanup() {
    if (state && state != (void*)-1) {
        shmdt(state);
        state = NULL;
    }

    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        shm_id = -1;
    }

    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        sem_id = -1;
    }

    if (msg_id != -1) {
        msgctl(msg_id, IPC_RMID, NULL);
        msg_id = -1;
    }
    unlink(FIFO_PATH);
    unlink(CLOSE_FIFO);
}
