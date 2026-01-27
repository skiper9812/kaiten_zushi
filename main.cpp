#include "ipc_manager.h"
#include "manager.h"
#include "chef.h"
#include "service.h"
#include "client.h"

volatile sig_atomic_t terminate_flag = 0;
volatile sig_atomic_t evacuate_flag = 0;

void sigint_handler(int sig) {
    terminate_flag = 1;
    kill(0, SIGRTMIN);
}

void terminate_handler(int sig) {
    terminate_flag = 1;
}

void evacuation_handler(int sig) {
    evacuate_flag = 1;
}

int main() {
    struct sigaction sa = { 0 };

    //SIGINT
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    //SIGRTMIN
    sa.sa_handler = terminate_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGRTMIN, &sa, NULL);

    //SIGTERM
    sa.sa_handler = evacuation_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    srand(time(0));

    // 1. Inicjalizacja IPC
    CHECK_ERR(ipc_init(), ERR_IPC_INIT, "IPC initialization");

    // 2. Pobranie wskaŸnika do stanu restauracji
    RestaurantState* state = get_state();
    CHECK_NULL(state, ERR_IPC_INIT, "Failed to get RestaurantState pointer");


    // 2. Utworzenie procesu loggera
    pid_t logger_pid = fork();
    if (logger_pid == 0) {
        // w procesie potomnym uruchom logger
        logger_loop("logs/simulation.log");  // blokuje do zamkniêcia FIFO
        _exit(0);
    }

    // 3. Utworzenie procesu managera
    pid_t manager_pid = fork();
    if (manager_pid == 0) {
        start_manager();  // pêtla testowa / minimalna
        _exit(0);
    }

    // 4.1 Utworzenie procesu szefa kuchni
    pid_t chef_pid = fork();
    if (chef_pid == 0) {
        start_chef();  // pêtla testowa
        _exit(0);
    }

    pid_t service_pid = fork();
    if (service_pid == 0) {
        start_service();  // pêtla testowa
        _exit(0);
    }

    // 5. Utworzenie generatora klientów
    pid_t client_pid = fork();
    if (client_pid == 0) {
        start_clients();  // pêtla testowa
        _exit(0);
    }

    

    // 6. Opcjonalnie: ograniczenie pêtli do X iteracji w start_*
    // lub sleep() w main() ¿eby procesy mog³y dzia³aæ

    // 7. Czekanie na zakoñczenie procesów
    for (;;) {
        pid_t pid = wait(NULL); // dowolny potomek
        if (pid == -1) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break; // nie ma dzieci
        }
    }

    // 4. Sprz¹tanie IPC
    ipc_cleanup();

    return 0;
}
