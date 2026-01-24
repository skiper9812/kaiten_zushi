#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

extern bool fatal_error;
extern volatile sig_atomic_t terminate_flag;

// Kody b³êdów specyficzne dla projektu
typedef enum {
    ERR_IPC_INIT,       // b³¹d inicjalizacji IPC
    ERR_IPC_MSG,        // b³¹d operacji na kolejce komunikatów
    ERR_SEM_OP,         // b³¹d operacji na semaforze
    ERR_MEM_ALLOC,      // b³¹d alokacji pamiêci
    ERR_FILE_IO,        // b³¹d operacji plikowej
    ERR_INVALID_INPUT,  // niepoprawne dane wejœciowe
    ERR_OVERFLOW,       // przepe³nienie zasobu
    ERR_UNKNOWN         // nieznany b³¹d
} ErrorCode;

// Funkcja obs³ugi b³êdów
// Wyœwietla komunikat, opcjonalnie raportuje errno i decyduje o dalszym dzia³aniu programu
void handle_error(ErrorCode code, const char* msg, int saved_errno);

// Makro u³atwiaj¹ce sprawdzanie wyników funkcji systemowych
#define CHECK_ERR(call, code, msg)                      \
    ({                                                  \
        bool result = false;                            \
        if ((call) == -1) {                             \
            int saved_errno = errno;                    \
            handle_error(code, msg, saved_errno);       \
            result = true;                              \
        } else {                                        \
            result = false;                             \
        }                                               \
        result;                                         \
    })

#define CHECK_NULL(ptr, code, msg)                 \
    if ((ptr) == NULL) handle_error(code, msg, 0);

#define CHECK_BLOCKING(call, code, msg)          \
    ({                                                \
        decltype(call) _res = (call);                 \
        if (CHECK_ERR(_res, code, msg)) ;             \
        else if (_res == -1 && errno == EINTR && terminate_flag) { \
                                                      \
        }                                             \
        _res;                                         \
    })


