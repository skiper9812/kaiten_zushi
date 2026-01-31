#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

// Strategy for error handling
typedef enum {
    ERR_DECISION_IGNORE,  // Log and continue
    ERR_DECISION_RETRY,   // Retry operation (e.g. on EINTR)
    ERR_DECISION_FATAL    // Abort program
} ErrorDecision;

extern bool fatalError;
extern volatile sig_atomic_t terminate_flag;
extern volatile sig_atomic_t evacuate_flag;

// Project-specific error codes
typedef enum {
    ERR_IPC_INIT,
    ERR_IPC_MSG,
    ERR_SEM_OP,
    ERR_MEM_ALLOC,
    ERR_FILE_IO,
    ERR_INVALID_INPUT,
    ERR_OVERFLOW,
    ERR_UNKNOWN
} ErrorCode;

// Handles error reporting and decision making
ErrorDecision handleError(ErrorCode code, const char* msg, int savedErrno);

// Macro to check system call result with automatic errno capture
#define CHECK_ERR(call, code, msg)                     \
    ({                                                 \
        ErrorDecision _dec = ERR_DECISION_IGNORE;      \
        if ((call) == -1) {                            \
            int savedErrno = errno;                    \
            _dec = handleError(code, msg, savedErrno); \
        }                                              \
        _dec;                                          \
    })

// Macro to check pointers for NULL
#define CHECK_NULL(ptr, code, msg)                     \
    if ((ptr) == NULL) handleError(code, msg, 0);
