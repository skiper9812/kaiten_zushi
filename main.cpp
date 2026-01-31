#include "ipc_manager.h"
#include "manager.h"
#include "chef.h"
#include "service.h"
#include "client.h"
#include "belt.h"
#include "reports.h"

volatile sig_atomic_t terminate_flag = 0;
volatile sig_atomic_t evacuate_flag = 0;

// Handles graceful shutdown signal (SIGINT)
void sigintHandler(int sig) {
    terminate_flag = 1;
    kill(0, SIGTERM);  // Propagate to all children
}

// Handles emergency evacuation signal
void evacuationHandler(int sig) {
    evacuate_flag = 1;
}

int main() {
    struct sigaction sa = { 0 };

    sa.sa_handler = sigintHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = evacuationHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags &= ~SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    
    // Ignore harmless signals globally
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    
    srand(time(0));

    CHECK_ERR(ipcInit(), ERR_IPC_INIT, "IPC initialization");

    RestaurantState* state = getState();
    CHECK_NULL(state, ERR_IPC_INIT, "Failed to get RestaurantState pointer");
    
    // Monitors process pauses for accurate timekeeping
    startPauseMonitor();

    // --- Spawn Subprocesses ---

    pid_t loggerPid = fork();
    if (loggerPid == 0) {
        signal(SIGINT, SIG_IGN); 
        signal(SIGTERM, SIG_IGN);
        loggerLoop("logs/simulation.log");
        _exit(0);
    }

    pid_t managerPid = fork();
    if (managerPid == 0) {
        signal(SIGINT, SIG_IGN);
        startManager();
        _exit(0);
    }

    pid_t beltPid = fork();
    if (beltPid == 0) {
        signal(SIGINT, SIG_IGN);
        startBelt();
        _exit(0);
    }

    pid_t chefPid = fork();
    if (chefPid == 0) {
        signal(SIGINT, SIG_IGN);
        startChef();
        _exit(0);
    }

    pid_t servicePid = fork();
    if (servicePid == 0) {
        signal(SIGINT, SIG_IGN);
        startService();
        _exit(0);
    }

    pid_t clientPid = fork();
    if (clientPid == 0) {
        signal(SIGINT, SIG_IGN);
        signal(SIGUSR1, SIG_IGN);
        signal(SIGUSR2, SIG_IGN);
        startClients();
        _exit(0);
    }

    // Wait for all children to exit
    for (;;) {
        pid_t pid = wait(NULL);
        if (pid == -1) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
        }
    }

    // Print Final Statistics
    printAllReports(state);

    ipcCleanup();

    return 0;
}
