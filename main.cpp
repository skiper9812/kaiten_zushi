#include "ipc_manager.h"
#include "manager.h"
#include "service.h"
#include "chef.h"
#include "client.h"

int main() {
    // 1. Inicjalizacja IPC
    CHECK_ERR(ipc_init(), ERR_IPC_INIT, "IPC initialization");

    // 2. Pobranie wskaŸnika do stanu restauracji
    RestaurantState* state = get_state();
    CHECK_NULL(state, ERR_IPC_INIT, "Failed to get RestaurantState pointer");

    // 3. Weryfikacja pól struktury
    printf("Restaurant mode: %d\n", state->restaurantMode);
    printf("Current guest count: %d\n", state->currentGuestCount);
    printf("Next guest ID: %d\n", state->nextGuestID);
    printf("Belt head: %d, Belt tail: %d\n", state->beltHead, state->beltTail);

    printf("\nTables:\n");
    for (int i = 0; i < TABLE_COUNT; ++i) {
        printf("Table %d: capacity=%d, isOccupied=%d, groupID=%d, groupSize=%d\n",
            state->tables[i].tableID,
            state->tables[i].capacity,
            state->tables[i].isOccupied,
            state->tables[i].groupID,
            state->tables[i].groupSize);
    }

    // 4. Sprz¹tanie IPC
    ipc_cleanup();

    return 0;
}
