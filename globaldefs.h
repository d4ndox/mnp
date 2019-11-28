#ifndef GLOBALDEFS_H
#define GLOBALDEFS_H

#define MAX_DATA_SIZE   (2048)
#define VERSION         "0.0.1"
#define DEBUG           (1)
#define CONFIG_FILE     ".mnp.ini"
extern int verbose;

struct MemoryStruct {
  char *memory;
  size_t size;
};

struct Config {
    const char* rpc_user;
    const char* rpc_password;
    const char* rpc_host;
    const char* rpc_port;
    const char* mnp_daemon;
    const char* mnp_verbose;
};

#define CONTENT_TYPE    "Content-Type: application/json"
#define GET_VERSION     "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_version\"}" 

#endif
