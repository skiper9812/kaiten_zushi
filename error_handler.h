#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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
#define CHECK_ERR(call, code, msg)         \
    do {                                   \
        if ((call) == -1) {                \
            int saved_errno = errno;       \
            handle_error(code, msg, saved_errno); \
        }                                  \
    } while (0)

#define CHECK_NULL(ptr, code, msg) \
    if ((ptr) == NULL) handle_error(code, msg, 0)
