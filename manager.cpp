#include "manager.h"

void send_close_signal() {
    int fd = open(CLOSE_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;  // brak czytelników jeszcze
    const char* msg = "CLOSE_RESTAURANT";
    write(fd, msg, strlen(msg) + 1);
    close(fd);
}

void start_manager() {
    for (;;) {
        // obs³uga sygna³ów steruj¹cych
        // zapis do logu
    }
}
