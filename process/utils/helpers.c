#include "helpers.h"

#include <stdarg.h>

void debug_print(
    FILE* stream,
    const char* __restrict format, ...)
{
    if (DEBUG == 0) return;

    va_list ap;
    va_start(ap, format);

    vfprintf(stream, format, ap);

    va_end(ap);
}
