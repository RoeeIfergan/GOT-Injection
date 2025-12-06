
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/mman.h>

#include <dlfcn.h>
#include <link.h>
#include <stdlib.h>

#include "utils/got_injection.h"

static int (*real_printf)(const char *fmt, ...) = NULL;

/* -------- our replacement printf -------- */

static int my_printf(const char *fmt, ...)
{
    if (!real_printf) {
        // Fallback: resolve original printf if GOT hook didn't set it
        real_printf = dlsym(RTLD_NEXT, "printf");
        if (!real_printf) {
            // If this fails, avoid recursion and just bail
            return -1;
        }
    }

    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    real_printf("[HOOKED] %s", buf);

    return n;
}

/* -------- constructor -------- */

__attribute__((constructor))
static void injected_init(void)
{
    fprintf(stderr, "[libhook] constructor in pid=%d\n", getpid());

    if (hook_plt_symbol("printf", (void *)my_printf,
                        (void **)&real_printf) == 0) {
        fprintf(stderr,
                "[libhook] GOT hook for printf installed, real_printf=%p\n",
                real_printf);
    } else {
        fprintf(stderr, "[libhook] FAILED to hook printf\n");
    }
}
