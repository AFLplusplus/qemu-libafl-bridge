#pragma once

#include "qemu/error-report.h"

#define SYX_PRINTF(format, ...)                                                \
    fprintf(stderr, ("[QEMU-SYX] " format), ##__VA_ARGS__)

#ifdef CONFIG_DEBUG_SYX
#define SYX_DEBUG(format, ...)                                                 \
    fprintf(stderr, ("[QEMU-SYX] DEBUG: " format), ##__VA_ARGS__)
#else
#define SYX_DEBUG(format, ...)
#endif

#define SYX_WARNING(format, ...)                                               \
    warn_report(("[QEMU-SYX] " format), ##__VA_ARGS__)

#define SYX_ERROR(format, ...)                                                 \
    error_report(("[QEMU-SYX] " format), ##__VA_ARGS__)
