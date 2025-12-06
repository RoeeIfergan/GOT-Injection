// inject_hook.c
// Build: gcc -shared -fPIC -o libhook.so inject_hook.c -ldl

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/mman.h>

#include <dlfcn.h>
#include <link.h>
#include <elf.h>

// AArch64 relocation type for PLT entries
#define MY_JUMP_SLOT R_AARCH64_JUMP_SLOT

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

/* -------- GOT/PLT patching helpers -------- */

struct hook_ctx {
    const char *symbol_name;    // "printf"
    void *new_func;             // my_printf
    void **orig_func;           // &real_printf
    int patched_count;
};

static int make_writable(void *addr)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return -1;

    uintptr_t p = (uintptr_t)addr;
    uintptr_t page_start = p & ~(page_size - 1);

    if (mprotect((void *)page_start, page_size,
                 PROT_READ | PROT_WRITE) != 0) {
        perror("[libhook] mprotect");
        return -1;
    }
    return 0;
}

static int phdr_callback(struct dl_phdr_info *info,
                         size_t size,
                         void *data)
{
    (void)size;
    struct hook_ctx *ctx = (struct hook_ctx *)data;

    Elf64_Addr base = info->dlpi_addr;
    Elf64_Dyn *dyn = NULL;

    // Find PT_DYNAMIC for this object
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn *)(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn)
        return 0;   // no dynamic section, skip

    Elf64_Addr symtab_addr = 0;
    Elf64_Addr strtab_addr = 0;
    Elf64_Addr jmprel_addr = 0;
    Elf64_Xword pltrelsz   = 0;
    Elf64_Sxword pltrel_type = 0;

    // IMPORTANT: on most modern AArch64 glibc, these d_ptr values
    // are already *absolute* runtime addresses, not base-relative.
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_SYMTAB:
            symtab_addr = d->d_un.d_ptr;
            break;
        case DT_STRTAB:
            strtab_addr = d->d_un.d_ptr;
            break;
        case DT_JMPREL:
            jmprel_addr = d->d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            pltrelsz = d->d_un.d_val;
            break;
        case DT_PLTREL:
            pltrel_type = d->d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (!symtab_addr || !strtab_addr || !jmprel_addr || !pltrelsz)
        return 0;

    if (pltrel_type != DT_RELA) {
        // AArch64 PLT should use RELA; skip other types.
        return 0;
    }

    Elf64_Sym  *symtab = (Elf64_Sym *)symtab_addr;
    const char *strtab = (const char *)strtab_addr;
    Elf64_Rela *rela   = (Elf64_Rela *)jmprel_addr;
    size_t rela_count  = pltrelsz / sizeof(Elf64_Rela);

    // Optional: debug which object we're touching
    const char *objname = (info->dlpi_name && info->dlpi_name[0])
                          ? info->dlpi_name : "<main>";
    fprintf(stderr, "[libhook] scanning object: %s\n", objname);

    for (size_t i = 0; i < rela_count; i++) {
        Elf64_Rela *r = &rela[i];
        unsigned long sym_idx = ELF64_R_SYM(r->r_info);
        unsigned long type    = ELF64_R_TYPE(r->r_info);

        if (type != MY_JUMP_SLOT)
            continue;

        Elf64_Sym *sym   = &symtab[sym_idx];
        const char *name = strtab + sym->st_name;

        if (strcmp(name, ctx->symbol_name) != 0)
            continue;

        void **got_entry = (void **)(base + r->r_offset);
        if (!got_entry)
            continue;

        if (ctx->orig_func && *ctx->orig_func == NULL) {
            *ctx->orig_func = *got_entry;   // save original printf
        }

        if (make_writable(got_entry) != 0) {
            fprintf(stderr, "[libhook] failed to mprotect GOT for %s in %s\n",
                    ctx->symbol_name, objname);
            continue;
        }

        fprintf(stderr,
                "[libhook] patching %s in %s: %p -> %p\n",
                ctx->symbol_name, objname,
                *got_entry, ctx->new_func);

        *got_entry = ctx->new_func;
        ctx->patched_count++;
    }

    return 0;   // keep iterating all DSOs
}

static int hook_plt_symbol(const char *symbol_name,
                           void *new_func,
                           void **orig_func)
{
    struct hook_ctx ctx = {
        .symbol_name   = symbol_name,
        .new_func      = new_func,
        .orig_func     = orig_func,
        .patched_count = 0,
    };

    dl_iterate_phdr(phdr_callback, &ctx);

    return (ctx.patched_count > 0) ? 0 : -1;
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
