#ifndef GLOBALDEFS_H
#define GLOBALDEFS_H

#define MAX_DATA_SIZE   (2048)
#define VERSION         "0.0.1"
#define DEBUG           (0)
#define CONFIG_FILE     ".mnp.ini"
extern int verbose;
extern int daemonflag;

struct MemoryStruct {
  char *memory;
  size_t size;
};

struct Config {
    const char  *rpc_user;
    const char  *rpc_password;
    const char  *rpc_host;
    const char  *rpc_port;
    const char  *mnp_daemon;
    const char  *mnp_verbose;
    const char  *mnp_account;
};

#define CONTENT_TYPE    "Content-Type: application/json"
#define GET_VERSION     "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_version\"}" 
#define GET_HEIGHT      "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_height\"}"
#define MAJOR_MASK      (0xFFFF0000)
#define MINOR_MASK      (0x0000FFFF)

#endif
