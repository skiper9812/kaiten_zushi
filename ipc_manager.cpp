#include "ipc_manager.h"

key_t SHM_KEY = -1;
key_t SEM_KEY = -1;
key_t MSG_KEY = -1;

static int shm_id = -1;
static int sem_id = -1;
static int msg_id = -1;
static RestaurantState* state = NULL;

static int fifo_fd_read = -1;

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

/* ---------- INIT FIFO ---------- */

void fifo_init() {
    // jeœli nie istnieje, utwórz FIFO
    if (access(FIFO_PATH, F_OK) == -1) {
        if (mkfifo(FIFO_PATH, 0666) == -1) {
            CHECK_ERR(-1, ERR_IPC_INIT, "mkfifo");
        }
    }
}

void fifo_init_close_signal() {
    // ignoruj b³¹d, jeœli FIFO ju¿ istnieje
    mkfifo(CLOSE_FIFO, 0666);
}

/* ---------- READ ---------- */

int fifo_open_read() {
    fifo_fd_read = open(FIFO_PATH, O_RDONLY);
    CHECK_ERR(fifo_fd_read, ERR_IPC_INIT, "fifo open read");
    return fifo_fd_read;
}

void fifo_close_read() {
    if (fifo_fd_read != -1) {
        close(fifo_fd_read);
        fifo_fd_read = -1;
    }
}

/* ---------- SEND LOG ---------- */

void fifo_log(const char* msg) {
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1) {
        // jeœli nie da siê otworzyæ FIFO, logujemy do stderr, ale nie zabijamy procesu
        fprintf(stderr, "FIFO log error: %s\n", strerror(errno));
        return;
    }

    size_t len = strlen(msg);
    if (write(fd, msg, len) == -1) {
        fprintf(stderr, "FIFO write error: %s\n", strerror(errno));
    }

    close(fd);
}

/* ---------- LOGGER LOOP ---------- */

void logger_loop(const char* filename) {
    fifo_open_read();

    FILE* file = fopen(filename, "w");
    CHECK_NULL(file, ERR_FILE_IO, "fopen log file");

    char buffer[256];
    ssize_t n;

    while ((n = read(fifo_fd_read, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0'; // null-terminate
        fprintf(file, "%s\n", buffer);
        fflush(file);
    }

    fclose(file);
    fifo_close_read();
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
    shm_id = shmget(SHM_KEY, sizeof(RestaurantState), IPC_CREAT | 0666);
    CHECK_ERR(shm_id, ERR_IPC_INIT, "shmget");

    state = (RestaurantState*)shmat(shm_id, NULL, 0);
    CHECK_NULL(state, ERR_IPC_INIT, "shmat");

    /* MSG */
    msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    CHECK_ERR(msg_id, ERR_IPC_INIT, "msgget premium");

    /* SEM */
    sem_id = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0666);
    CHECK_ERR(sem_id, ERR_IPC_INIT, "semget");

    sem_set(SEM_MUTEX_STATE, 1);      // mutex binarny
    sem_set(SEM_BELT_SLOTS, BELT_SIZE);
    sem_set(SEM_BELT_ITEMS, 0);
    sem_set(SEM_TABLES, TOTAL_SEATS);

    /* FIFO */
    fifo_init();
    fifo_init_close_signal();

    /* INIT STATE */
    P(SEM_MUTEX_STATE);

    memset(state, 0, sizeof(RestaurantState));

    state->restaurantMode = OPEN;
    state->beltHead = 0;
    state->beltTail = 0;
    state->currentDishCount = 0;

    state->nextGuestID = 1;
    state->nextGroupID = 1;
    state->nextDishID = 1;

    for (int i = 0; i < TABLE_COUNT; ++i) {
        state->tables[i].tableID = i;

        if (i < X1)
            state->tables[i].capacity = 1;
        else if (i < X1 + X2)
            state->tables[i].capacity = 2;
        else if (i < X1 + X2 + X3)
            state->tables[i].capacity = 3;
        else
            state->tables[i].capacity = 4;

        state->tables[i].isOccupied = 0;
        state->tables[i].groupID = -1;
        state->tables[i].groupSize = 0;
    }

    V(SEM_MUTEX_STATE);

    return 0;
}

/* ---------- ACCESS ---------- */

RestaurantState* get_state() {
    return state;
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
}
