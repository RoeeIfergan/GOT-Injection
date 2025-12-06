#ifndef PROCESS_GOT_INJECTION_H
#define PROCESS_GOT_INJECTION_H

int hook_plt_symbol(const char *symbol_name,
                           void *new_func,
                           void **orig_func);
#endif //PROCESS_GOT_INJECTION_H