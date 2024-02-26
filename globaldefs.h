#ifndef GLOBALDEFS_H
#define GLOBALDEFS_H

#define MAX_DATA_SIZE   (2048)
#define MAX_ADDR_SIZE   (95)
#define MAX_PAYID_SIZE  (16)
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
    const char  *cfg_workdir;
    const char  *cfg_mode;
};

#define GET_HEIGHT_CMD  "get_height"
#define BC_HEIGHT_FILE  "bc_height"
#define GET_BALANCE_CMD "get_balance"
#define BALANCE_FILE    "balance"
#define GET_SUBADDR_CMD "get_address"

#define MK_IADDR_CMD    "make_integrated_address"

#define SETUP_DIR       "setup"
#define SETUP_TRANSFER  "transfer"
#define SETUP_PAYMENT   "payment"

#define TRANSFER_DIR    "transfer"
#define PAYMENT_DIR     "payment"

#define NOPARAMS        NULL
#define CONTENT_TYPE    "Content-Type: application/json"
#define JSON_RPC        "2.0"
#define GET_VERSION_CMD "get_version"
#define MAJOR_MASK      (0xFFFF0000)
#define MINOR_MASK      (0x0000FFFF)
#define SLEEPTIME       (5000)
#endif
