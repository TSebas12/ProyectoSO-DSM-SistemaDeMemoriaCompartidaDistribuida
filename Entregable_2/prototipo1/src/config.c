#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_config.h"

static void trim(char *s){char*p=s;while(isspace((unsigned char)*p))p++;if(p!=s)memmove(s,p,strlen(p)+1);size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]='\0';}
static int pu(const char*v,unsigned long*out){char*e;errno=0;unsigned long x=strtoul(v,&e,10);if(errno||*e!='\0'||e==v)return -1;*out=x;return 0;}
static int pev(const char*v){unsigned long u;if(!strcmp(v,"lru"))return 0;if(!strcmp(v,"clock"))return 1;if(pu(v,&u)==0&&u<=1)return (int)u;return -1;}
static int psc(const char*v){unsigned long u;if(!strcmp(v,"round_robin"))return 0;if(!strcmp(v,"least_loaded"))return 1;if(pu(v,&u)==0&&u<=1)return (int)u;return -1;}
static int pll(const char*v){unsigned long u;if(!strcmp(v,"error"))return 0;if(!strcmp(v,"warn"))return 1;if(!strcmp(v,"info"))return 2;if(!strcmp(v,"debug"))return 3;if(!strcmp(v,"trace"))return 4;if(pu(v,&u)==0&&u<=4)return (int)u;return -1;}

dsm_status_t dsm_config_defaults(dsm_config_t*cfg){if(!cfg)return DSM_ERR_INVAL;memset(cfg,0,sizeof(*cfg));
cfg->config_version=1;cfg->page_size=4096;cfg->node_memory=1048576;cfg->window_size=268435456;cfg->max_nodes=16;cfg->node_id=0;strcpy(cfg->bind_address,"0.0.0.0");cfg->listen_port=27300;cfg->control_port=27301;cfg->heartbeat_port=27302;cfg->web_port=27303;cfg->web_enabled=1;strcpy(cfg->seed_host,"127.0.0.1");cfg->seed_port=27300;cfg->heartbeat_interval_ms=1000;cfg->heartbeat_timeout_ms=3000;cfg->network_timeout_ms=2000;cfg->connect_timeout_ms=2000;cfg->retry_count=3;cfg->retry_backoff_ms=250;cfg->max_message_size=8192;cfg->thread_pool_size=4;cfg->fault_queue_size=1024;cfg->eviction_policy=0;cfg->log_level=2;strcpy(cfg->log_file,"logs/dsm.log");cfg->stats_interval_ms=5000;cfg->barrier_timeout_ms=30000;cfg->replication_factor=0;cfg->checkpoint_interval_ms=0;cfg->scheduler_policy=0;cfg->port_range_start=27300;cfg->port_range_end=27399;return DSM_OK;}

dsm_status_t dsm_config_load(const char*path,dsm_config_t*cfg){
if(!path||!cfg)return DSM_ERR_INVAL;
dsm_config_defaults(cfg);
FILE*f=fopen(path,"r");
if(!f)return DSM_ERR_NOENT;
char line[1024];
while(fgets(line,sizeof(line),f)){char*hash=strchr(line,'#');if(hash)*hash='\0';char*eq=strchr(line,'=');if(!eq)continue;*eq='\0';char key[128],val[512];size_t klen=(size_t)(eq-line);if(klen>=sizeof(key))klen=sizeof(key)-1;memcpy(key,line,klen);key[klen]='\0';snprintf(val,sizeof(val),"%s",eq+1);trim(key);trim(val);if(!key[0])continue;
unsigned long u;if(!strcmp(key,"config_version")&&pu(val,&u)==0)cfg->config_version=(uint32_t)u;
else if(!strcmp(key,"node_id")&&pu(val,&u)==0)cfg->node_id=(uint32_t)u;
else if(!strcmp(key,"bind_address"))snprintf(cfg->bind_address,sizeof(cfg->bind_address),"%.63s",val);
else if(!strcmp(key,"listen_port")&&pu(val,&u)==0)cfg->listen_port=(uint16_t)u;
else if(!strcmp(key,"control_port")&&pu(val,&u)==0)cfg->control_port=(uint16_t)u;
else if(!strcmp(key,"heartbeat_port")&&pu(val,&u)==0)cfg->heartbeat_port=(uint16_t)u;
else if(!strcmp(key,"web_port")&&pu(val,&u)==0)cfg->web_port=(uint16_t)u;
else if(!strcmp(key,"web_enabled")&&pu(val,&u)==0)cfg->web_enabled=(uint16_t)u;
else if(!strcmp(key,"seed_host"))snprintf(cfg->seed_host,sizeof(cfg->seed_host),"%.255s",val);
else if(!strcmp(key,"seed_port")&&pu(val,&u)==0)cfg->seed_port=(uint16_t)u;
else if(!strcmp(key,"page_size")&&pu(val,&u)==0)cfg->page_size=(uint32_t)u;
else if(!strcmp(key,"node_memory")&&pu(val,&u)==0)cfg->node_memory=(uint64_t)u;
else if(!strcmp(key,"window_size")&&pu(val,&u)==0)cfg->window_size=(uint64_t)u;
else if(!strcmp(key,"max_nodes")&&pu(val,&u)==0)cfg->max_nodes=(uint32_t)u;
else if(!strcmp(key,"heartbeat_interval_ms")&&pu(val,&u)==0)cfg->heartbeat_interval_ms=(uint32_t)u;
else if(!strcmp(key,"heartbeat_timeout_ms")&&pu(val,&u)==0)cfg->heartbeat_timeout_ms=(uint32_t)u;
else if(!strcmp(key,"network_timeout_ms")&&pu(val,&u)==0)cfg->network_timeout_ms=(uint32_t)u;
else if(!strcmp(key,"connect_timeout_ms")&&pu(val,&u)==0)cfg->connect_timeout_ms=(uint32_t)u;
else if(!strcmp(key,"retry_count")&&pu(val,&u)==0)cfg->retry_count=(uint32_t)u;
else if(!strcmp(key,"retry_backoff_ms")&&pu(val,&u)==0)cfg->retry_backoff_ms=(uint32_t)u;
else if(!strcmp(key,"max_message_size")&&pu(val,&u)==0)cfg->max_message_size=(uint32_t)u;
else if(!strcmp(key,"thread_pool_size")&&pu(val,&u)==0)cfg->thread_pool_size=(uint32_t)u;
else if(!strcmp(key,"fault_queue_size")&&pu(val,&u)==0)cfg->fault_queue_size=(uint32_t)u;
else if(!strcmp(key,"eviction_policy")){int e=pev(val);if(e>=0)cfg->eviction_policy=(uint32_t)e;}
else if(!strcmp(key,"log_level")){int e=pll(val);if(e>=0)cfg->log_level=(uint32_t)e;}
else if(!strcmp(key,"log_file"))snprintf(cfg->log_file,sizeof(cfg->log_file),"%.255s",val);
else if(!strcmp(key,"stats_interval_ms")&&pu(val,&u)==0)cfg->stats_interval_ms=(uint32_t)u;
else if(!strcmp(key,"barrier_timeout_ms")&&pu(val,&u)==0)cfg->barrier_timeout_ms=(uint32_t)u;
else if(!strcmp(key,"replication_factor")&&pu(val,&u)==0)cfg->replication_factor=(uint32_t)u;
else if(!strcmp(key,"checkpoint_interval_ms")&&pu(val,&u)==0)cfg->checkpoint_interval_ms=(uint32_t)u;
else if(!strcmp(key,"scheduler_policy")){int e=psc(val);if(e>=0)cfg->scheduler_policy=(uint32_t)e;}
else if(!strcmp(key,"port_range_start")&&pu(val,&u)==0)cfg->port_range_start=(uint32_t)u;
else if(!strcmp(key,"port_range_end")&&pu(val,&u)==0)cfg->port_range_end=(uint32_t)u;
}
fclose(f);return DSM_OK;}

dsm_status_t dsm_config_validate(const dsm_config_t*cfg){
if(!cfg)return DSM_ERR_INVAL;
if(cfg->config_version!=1)return DSM_ERR_NOTSUP;
if(cfg->node_id>=cfg->max_nodes)return DSM_ERR_INVAL;
if(cfg->page_size<4096||(cfg->page_size&(cfg->page_size-1)))return DSM_ERR_INVAL;
if(cfg->node_memory%cfg->page_size)return DSM_ERR_INVAL;
if(cfg->node_memory/cfg->page_size<16)return DSM_ERR_INVAL;
if(cfg->window_size<cfg->node_memory)return DSM_ERR_INVAL;
if(cfg->window_size%cfg->page_size)return DSM_ERR_INVAL;
uint16_t ports[4]={cfg->listen_port,cfg->control_port,cfg->heartbeat_port,cfg->web_port};
for(int i=0;i<4;i++)if(ports[i]<cfg->port_range_start||ports[i]>cfg->port_range_end)return DSM_ERR_INVAL;
for(int i=0;i<4;i++)for(int j=i+1;j<4;j++)if(ports[i]==ports[j])return DSM_ERR_INVAL;
if(cfg->heartbeat_timeout_ms<2*cfg->heartbeat_interval_ms)return DSM_ERR_INVAL;
if(cfg->max_message_size<cfg->page_size+256)return DSM_ERR_INVAL;
if(cfg->retry_count>10)return DSM_ERR_INVAL;
if(cfg->thread_pool_size<1||cfg->thread_pool_size>64)return DSM_ERR_INVAL;
if(cfg->log_level>4)return DSM_ERR_INVAL;
if(cfg->replication_factor>cfg->max_nodes-1)return DSM_ERR_INVAL;
return DSM_OK;}

dsm_status_t dsm_config_save(const char*path,const dsm_config_t*cfg){
if(!path||!cfg)return DSM_ERR_INVAL;
FILE*f=fopen(path,"w");
if(!f)return DSM_ERR_PERM;
fprintf(f,"config_version = %u\n",cfg->config_version);
fprintf(f,"node_id = %u\n",cfg->node_id);
fprintf(f,"max_nodes = %u\n",cfg->max_nodes);
fprintf(f,"bind_address = %s\n",cfg->bind_address);
fprintf(f,"listen_port = %u\n",(unsigned)cfg->listen_port);
fprintf(f,"control_port = %u\n",(unsigned)cfg->control_port);
fprintf(f,"heartbeat_port = %u\n",(unsigned)cfg->heartbeat_port);
fprintf(f,"web_port = %u\n",(unsigned)cfg->web_port);
fprintf(f,"web_enabled = %u\n",(unsigned)cfg->web_enabled);
fprintf(f,"seed_host = %s\n",cfg->seed_host);
fprintf(f,"seed_port = %u\n",(unsigned)cfg->seed_port);
fprintf(f,"page_size = %u\n",cfg->page_size);
fprintf(f,"node_memory = %llu\n",(unsigned long long)cfg->node_memory);
fprintf(f,"window_size = %llu\n",(unsigned long long)cfg->window_size);
fprintf(f,"heartbeat_interval_ms = %u\n",cfg->heartbeat_interval_ms);
fprintf(f,"heartbeat_timeout_ms = %u\n",cfg->heartbeat_timeout_ms);
fprintf(f,"network_timeout_ms = %u\n",cfg->network_timeout_ms);
fprintf(f,"connect_timeout_ms = %u\n",cfg->connect_timeout_ms);
fprintf(f,"retry_count = %u\n",cfg->retry_count);
fprintf(f,"retry_backoff_ms = %u\n",cfg->retry_backoff_ms);
fprintf(f,"max_message_size = %u\n",cfg->max_message_size);
fprintf(f,"thread_pool_size = %u\n",cfg->thread_pool_size);
fprintf(f,"fault_queue_size = %u\n",cfg->fault_queue_size);
fprintf(f,"eviction_policy = %u\n",cfg->eviction_policy);
fprintf(f,"log_level = %u\n",cfg->log_level);
fprintf(f,"log_file = %s\n",cfg->log_file);
fprintf(f,"stats_interval_ms = %u\n",cfg->stats_interval_ms);
fprintf(f,"barrier_timeout_ms = %u\n",cfg->barrier_timeout_ms);
fprintf(f,"replication_factor = %u\n",cfg->replication_factor);
fprintf(f,"checkpoint_interval_ms = %u\n",cfg->checkpoint_interval_ms);
fprintf(f,"scheduler_policy = %u\n",cfg->scheduler_policy);
fprintf(f,"port_range_start = %u\n",cfg->port_range_start);
fprintf(f,"port_range_end = %u\n",cfg->port_range_end);
fclose(f);return DSM_OK;}

const char*dsm_log_level_name(uint32_t level){const char*n[]={"error","warn","info","debug","trace"};if(level>4)return "unknown";return n[level];}