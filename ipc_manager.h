#pragma once
#include "common.h"

#define CLIENT_REQ_QUEUE 'X'
#define SERVICE_REQ_QUEUE 'Y'
#define MAX_MSG_TEXT 128

extern int client_qid;
extern int service_qid;

// =====================================================
// IPC – protokó³ clients <-> reszta systemu
// =====================================================

enum ClientRequestType {
    REQ_ASSIGN_GROUP = 1,
    REQ_CONSUME_DISH,
    REQ_GROUP_DONE
};

enum ServiceRequestType {
    REQ_GET_GROUP = 1,
    REQ_GROUP_REJECT,
    REQ_GROUP_ASSIGNED
};

typedef struct {
    long mtype;
    ServiceRequestType type;
    int extraData;
} ServiceRequest;

typedef struct {
    long mtype;
    ClientRequestType type;
    pid_t pid;

    int groupID;
    int groupSize;
    int adultCount;
    int childCount;
    bool vipStatus;
    int eatenCount[COLOR_COUNT];
} ClientRequest;

typedef struct {
    long mtype;
    int pid;
    int groupID;
    int groupSize;
    int adultCount;
    int childCount;
    bool vipStatus;
    int dishesToEat;
} ClientResponse;

struct PremiumMsg {
    long mtype;
    int table_id;
    int dish_price;
};

// -----------------------------------------------------
// Globalne klucze i identyfikatory IPC
// -----------------------------------------------------

extern key_t SHM_KEY;
extern key_t SEM_KEY;
extern key_t MSG_KEY;

extern int shm_id;
extern int sem_id;
extern int msg_id;
extern RestaurantState* state;

extern int fifo_fd_write;
extern int fifo_fd_read;

// -----------------------------------------------------
// Funkcje dostêpu do stanu
// -----------------------------------------------------

RestaurantState* get_state();
void handle_manager_signal(int sig);

// -----------------------------------------------------
// Inicjalizacja / czyszczenie IPC
// -----------------------------------------------------

int ipc_init();
void ipc_cleanup();

// -----------------------------------------------------
// Semafory
// -----------------------------------------------------

 static void sem_set(int semnum, int val);
void P(int semnum);
void V(int semnum);

// -----------------------------------------------------
// Kolejki komunikatów (System V)
// -----------------------------------------------------

int queue_pop(int requiredSize, bool vipSuitable);
bool queue_push(int groupID, bool vipStatus);

int create_queue(char proj_id);
int connect_queue(char proj_id);
void remove_queue(char proj_id);

void queue_send_request(const ClientRequest& msg);
void queue_recv_request(ClientRequest& msg, long mtype = 0);

void queue_send_response(const ClientResponse& msg);
void queue_recv_response(ClientResponse& msg, long mtype = 0);

void queue_send_request(const ServiceRequest& msg);
void queue_recv_request(ServiceRequest& msg, long mtype = 0);

// -----------------------------------------------------
// Premium orders
// -----------------------------------------------------

void send_premium_order(int table_id, int price);
int recv_premium_order(PremiumMsg* msg);

// -----------------------------------------------------
// FIFO logger
// -----------------------------------------------------

void fifo_init();
void fifo_open_write();
void fifo_close_write();
void fifo_log(const char* msg);
void logger_loop(const char* filename);
void fifo_init_close_signal();
