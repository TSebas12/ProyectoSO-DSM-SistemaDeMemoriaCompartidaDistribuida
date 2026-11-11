#include <stdio.h>
#include <string.h>

#include "dsm_runtime.h"

void dsm_demo_page_run(dsm_runtime_t *rt) {
    if (!rt) return;
    /* touch page 0 */
    char *p = (char*)dsm_memory_window_base(&rt->mem);
    if (p) {
        strcpy(p, "hello dsm");
        printf("page0: %s\n", p);
    }
}