#ifndef GLOBALDEFS_H
#define GLOBALDEFS_H

#define _XOPEN_SOURCE 700
#define MAX_DATA_SIZE   (4096)
#define MAX_IADDR_SIZE  (106)
#define MAX_ADDR_SIZE   (96)
#define MAX_PAYID_SIZE  (16)
#define MAX_TXID_SIZE   (64)
#define VERSION         "0.1.2"
#define DEBUG           (0)
#define CONFIG_FILE     ".mnp.ini"
#define TMP_TXID_FILE   "/tmp/mnp.txid"
extern int verbose;

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
    const char  *cfg_pipe;
};

enum notify {
    NONE,
    TXPOOL,
    CONFIRMED,
    UNLOCKED,
};

#define GET_HEIGHT_CMD  "get_height"
#define BC_HEIGHT_FILE  "bc_height"
#define GET_BALANCE_CMD "get_balance"
#define BALANCE_FILE    "balance"
#define GET_SUBADDR_CMD "get_address"
#define NEW_SUBADDR_CMD "create_address"

#define MK_IADDR_CMD    "make_integrated_address"
#define SP_IADDR_CMD    "split_integrated_address"
#define MK_URI_CMD      "make_uri"

#define GET_TXID_CMD    "get_transfer_by_txid"

#define GET_PAYMENT_CMD "get_bulk_payments"

#define TRANSACTION_DIR "transactions"

#define PAYNULL         "0000000000000000"
#define NOPARAMS        NULL
#define CONTENT_TYPE    "Content-Type: application/json"
#define JSON_RPC        "2.0"
#define SLEEPTIME       (60)
#endif
