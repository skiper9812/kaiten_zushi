#include "ipc_manager.h"
#include "manager.h"
#include "chef.h"
#include "service.h"
#include "client.h"
#include "belt.h"
#include "reports.h"

volatile sig_atomic_t terminate_flag = 0;
volatile sig_atomic_t evacuate_flag = 0;

void sigintHandler(int sig) {
    terminate_flag = 1;
    kill(0, SIGTERM);  // Propagate to all children
}

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
    
    // Ignore SIGPIPE globally - prevent crash if logging fails
    signal(SIGPIPE, SIG_IGN);
    // Ignore control signals globally (children inherit SIG_IGN).
    // Only Manager (and Monitor) will override this.
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    
    srand(time(0));

    CHECK_ERR(ipcInit(), ERR_IPC_INIT, "IPC initialization");

    RestaurantState* state = getState();
    CHECK_NULL(state, ERR_IPC_INIT, "Failed to get RestaurantState pointer");
    
    // Start PauseMonitor thread in main process AFTER all forks
    // This ensures child processes don't inherit SIGCONT blocking/handling logic
    startPauseMonitor();

    pid_t loggerPid = fork();
    if (loggerPid == 0) {
        signal(SIGINT, SIG_IGN);  // Children ignore SIGINT, main propagates as SIGTERM
        signal(SIGTERM, SIG_IGN); // Logger ignores SIGTERM - exits only when pipe is closed (EOF)
        loggerLoop("logs/simulation.log");
        _exit(0);
    }

    pid_t managerPid = fork();
    if (managerPid == 0) {
        signal(SIGINT, SIG_IGN);
        // Manager NEEDS SIGUSR1/SIGUSR2 for control, so it will set its own handlers
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

    for (;;) {
        pid_t pid = wait(NULL);
        if (pid == -1) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
        }
    }

    // Print final reports before cleanup
    printAllReports(state);

    ipcCleanup();

    return 0;
}
