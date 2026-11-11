#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsm_api.h"
#include "dsm_config.h"
#include "dsm_log.h"
#include "dsm_runtime.h"

int main(int argc, char **argv) {
    const char *config_path = "config/dsm.conf";
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--config") && i+1<argc) config_path = argv[++i];
        else if (!strcmp(argv[i],"--check-config")) {
            dsm_config_t cfg; dsm_config_defaults(&cfg);
            if (dsm_config_load(config_path,&cfg)==DSM_OK && dsm_config_validate(&cfg)==DSM_OK) {
                printf("dsm.conf: OK\n");
                return 0;
            }
            printf("dsm.conf: FAIL\n");
            return 1;
        } else if (!strcmp(argv[i],"--version")) { printf("dsm_node 0.1\n"); return 0; }
    }
    dsm_config_t cfg; dsm_config_defaults(&cfg);
    if (dsm_config_load(config_path,&cfg)!=DSM_OK) { fprintf(stderr,"config load fail\n"); return 1; }
    if (dsm_config_validate(&cfg)!=DSM_OK) { fprintf(stderr,"config invalid\n"); return 1; }
    dsm_log_init(cfg.log_file, (int)cfg.log_level);
    dsm_runtime_t *rt = NULL;
    if (dsm_init(&cfg,&rt)!=DSM_OK) { fprintf(stderr,"init fail\n"); return 1; }
    dsm_create(rt); dsm_attach(rt);
    printf("node %u running\n", cfg.node_id);
    /* keep running */
    while (1) sleep(1);
    dsm_shutdown(rt);
    dsm_log_close();
    return 0;
}