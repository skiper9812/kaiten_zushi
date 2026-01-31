#pragma once
#include "common.h"

#define CLIENT_QUEUE_SIZE 256
#define SERVICE_QUEUE_SIZE 1024
#define PREMIUM_QUEUE_SIZE 1024

#define CLIENT_REQ_QUEUE 'X'
#define SERVICE_REQ_QUEUE 'Y'
#define PREMIUM_REQ_QUEUE 'Z'
// Pause Monitor
void startPauseMonitor();
#define MAX_MSG_TEXT 128

extern int clientQid;
extern int serviceQid;
extern int premiumQid;

// =====================================================
// IPC - protocol clients <-> rest of system
// =====================================================

enum ClientRequestType {
    REQ_ASSIGN_GROUP = 1,
    REQ_CONSUME_DISH,
    REQ_GROUP_FINISHED,
    REQ_BARRIER_CHECK
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

typedef struct {
    long mtype;
    int groupID;
    int dish;
} PremiumRequest;

// -----------------------------------------------------
// Global IPC keys and identifiers
// -----------------------------------------------------

extern key_t SHM_KEY;
extern key_t SEM_KEY;
extern key_t MSG_KEY;

extern int shmId;
extern int semId;
extern int msgId;
extern RestaurantState* state;

extern int fifoFdWrite;
extern int fifoFdRead;

// -----------------------------------------------------
// State access functions
// -----------------------------------------------------

RestaurantState* getState();
void handleManagerSignal(int sig);

// -----------------------------------------------------
// IPC initialization / cleanup
// -----------------------------------------------------

int ipcInit();
void ipcCleanup();

// -----------------------------------------------------
// Semaphores
// -----------------------------------------------------

void P(int semnum);
void V(int semnum);
int getSemValue(int semnum);

// -----------------------------------------------------
// Message queues (System V)
// -----------------------------------------------------

int queuePop(int requiredSize, bool vipSuitable);
bool queuePush(pid_t groupPid, bool vipStatus, int groupSize, int groupID);

int createQueue(char projId);
int connectQueue(char projId);
void removeQueue(char projId);

void queueSendRequest(const ClientRequest& msg);
void queueRecvRequest(ClientRequest& msg, long mtype = 0);
void queueSendResponse(const ClientResponse& msg);
void queueRecvResponse(ClientResponse& msg, long mtype = 0);

void queueSendRequest(const ServiceRequest& msg);
void queueRecvRequest(ServiceRequest& msg, long mtype = 0);

void queueSendRequest(const PremiumRequest& msg);
bool queueRecvRequest(PremiumRequest& msg, long mtype = 0);

// -----------------------------------------------------
// FIFO logger
// -----------------------------------------------------

void fifoInit();
void fifoOpenWrite();
void fifoCloseWrite();
void fifoLog(const char* msg);
void loggerLoop(const char* filename);
void fifoInitCloseSignal();
