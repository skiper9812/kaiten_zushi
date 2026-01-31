#pragma once
#include "common.h"

// Queue capacities
#define CLIENT_QUEUE_SIZE 256
#define SERVICE_QUEUE_SIZE 1024
#define PREMIUM_QUEUE_SIZE 1024

// Project IDs for ftok
#define CLIENT_REQ_QUEUE 'X'
#define SERVICE_REQ_QUEUE 'Y'
#define PREMIUM_REQ_QUEUE 'Z'

#define MAX_MSG_TEXT 128

// Starts the Pause Monitor thread
void startPauseMonitor();

extern int clientQid;
extern int serviceQid;
extern int premiumQid;

// ============================================================================
// MESSAGE DEFINITIONS
// ============================================================================

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

// Message structure for Service -> Client communication
typedef struct {
    long mtype;
    ServiceRequestType type;
    int extraData;
} ServiceRequest;

// Message structure for Client -> Service communication
typedef struct {
    long mtype;                 // Message type (e.g., ASSIGN, FINISHED)
    ClientRequestType type;
    pid_t pid;                  // Sender PID

    // Payload for group info
    int groupID;
    int groupSize;
    int adultCount;
    int childCount;
    bool vipStatus;
    int eatenCount[COLOR_COUNT]; // Stats for finish report
} ClientRequest;

// Message structure for Service -> Client responses (Legacy/Generic)
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

// Message structure for Premium orders
typedef struct {
    long mtype;
    int groupID;
    int dish; // Dish index desired
} PremiumRequest;


// ============================================================================
// GLOBAL IPC HANDLES
// ============================================================================

extern key_t SHM_KEY;
extern key_t SEM_KEY;
extern key_t MSG_KEY;

extern int shmId;
extern int semId;
extern int msgId;
extern RestaurantState* state;

extern int fifoFdWrite;
extern int fifoFdRead;


// Get global state pointer
RestaurantState* getState();

// Signal handler for Manager logic
void handleManagerSignal(int sig);


// Initialize all IPC primitives (SHM, Semaphores, Queues)
int ipcInit();

// Destroy all IPC primitives
void ipcCleanup();


// P (Wait/Decrement) Operation on Semaphore
void P(int semnum);

// V (Signal/Increment) Operation on Semaphore
void V(int semnum);

// Get current value of semaphore (safe/non-blocking)
int getSemValue(int semnum);


// Pushes a group into the waiting queue (VIP or Normal)
bool queuePush(pid_t groupPid, bool vipStatus, int groupSize, int groupID);

// Create Message Queue with specified Project ID
int createQueue(char projId);

// Connect to existing Message Queue
int connectQueue(char projId);

// Remove Message Queue
void removeQueue(char projId);


// --- Safe Message Queue Wrappers ---
// These wrappers handle retries, termination flags, and flow control (semaphores)

void queueSendRequest(const ClientRequest& msg);
void queueRecvRequest(ClientRequest& msg, long mtype = 0);
void queueSendResponse(const ClientResponse& msg);
void queueRecvResponse(ClientResponse& msg, long mtype = 0);

void queueSendRequest(const ServiceRequest& msg);
void queueRecvRequest(ServiceRequest& msg, long mtype = 0);

void queueSendRequest(const PremiumRequest& msg);
bool queueRecvRequest(PremiumRequest& msg, long mtype = 0);


// --- FIFO Logging ---

void fifoInit();
void fifoOpenWrite();
void fifoCloseWrite();
void fifoLog(const char* msg);
void loggerLoop(const char* filename);
void fifoInitCloseSignal();
