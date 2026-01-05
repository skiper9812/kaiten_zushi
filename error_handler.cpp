#define _CRT_SECURE_NO_WARNINGS
#include "error_handler.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void handle_error(ErrorCode code, const char* msg, int saved_errno) {
    fprintf(stderr, "[ERROR %d] %s (%s)\n", code, msg,
        saved_errno ? strerror(saved_errno) : "no errno");

    switch (code) {
        case ERR_IPC_INIT:
        case ERR_SEM_OP:
        case ERR_MEM_ALLOC:
        case ERR_IPC_MSG:
            exit(EXIT_FAILURE);
        case ERR_FILE_IO:
            break;
        case ERR_INVALID_INPUT:
        case ERR_OVERFLOW:
            break;
        default:
            exit(EXIT_FAILURE);
    }
}
