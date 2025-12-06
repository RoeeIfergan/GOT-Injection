#ifndef PROCESS_HELPERS_H
#define PROCESS_HELPERS_H

#include <stdio.h>

#define DEBUG 0

void debug_print(
    FILE* stream,
    const char* __restrict format, ...);

#endif //PROCESS_HELPERS_H