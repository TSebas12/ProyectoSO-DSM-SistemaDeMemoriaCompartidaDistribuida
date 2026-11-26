#ifndef DSM_LOG_H
#define DSM_LOG_H

#include <stdio.h>

typedef enum { DSM_LOG_ERROR=0, DSM_LOG_WARN, DSM_LOG_INFO, DSM_LOG_DEBUG, DSM_LOG_TRACE } dsm_log_level_t;

void dsm_log_init(const char *file, int level);
void dsm_log_close(void);
void dsm_log(int level, const char *fmt, ...);

#define DSM_LOG(l,...) dsm_log(l,__VA_ARGS__)

#endif /* DSM_LOG_H */