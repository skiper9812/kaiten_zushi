#include "ipc_manager.h"
#include "manager.h"
#include "chef.h"
#include "client.h"

int main() {
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

    // 4. Utworzenie procesu szefa kuchni
    pid_t chef_pid = fork();
    if (chef_pid == 0) {
        start_chef();  // pêtla testowa
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
    waitpid(logger_pid, NULL, 0);
    waitpid(manager_pid, NULL, 0);
    waitpid(chef_pid, NULL, 0);
    waitpid(client_pid, NULL, 0);

    // 4. Sprz¹tanie IPC
    ipc_cleanup();

    return 0;
}
