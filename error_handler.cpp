#define _CRT_SECURE_NO_WARNINGS
#include "error_handler.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

bool fatalError = 0;

// Centralized error handler
ErrorDecision handleError(ErrorCode code, const char* msg, int savedErrno) {
    // If system is terminating, suppress errors to allow clean exit
    if (terminate_flag || evacuate_flag)
        return ERR_DECISION_IGNORE;

    fprintf(stderr, "[ERROR %d] %s (%s)\n",
        code, msg,
        savedErrno ? strerror(savedErrno) : "no errno");

    // Retry on interrupted system calls
    if (savedErrno == EINTR)
        return ERR_DECISION_RETRY;

    switch (code) {
    case ERR_IPC_INIT:
    case ERR_SEM_OP:
    case ERR_MEM_ALLOC:
    case ERR_IPC_MSG:
        fatalError = 1;
        return ERR_DECISION_FATAL;

    default:
        return ERR_DECISION_IGNORE;
    }
}
