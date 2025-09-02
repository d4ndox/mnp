#ifndef GLOBALDEFS_H
#define GLOBALDEFS_H

#define _XOPEN_SOURCE 700
#define MAX_DATA_SIZE   (4096)
#define MAX_IADDR_SIZE  (106)
#define MAX_ADDR_SIZE   (96)
#define MAX_PAYID_SIZE  (16)
#define MAX_TXID_SIZE   (64)
#define VERSION         "0.1.4"
#define DEBUG           (0)
#define CONFIG_FILE     ".mnp.ini"
#define TMP_TXID_FILE   "/tmp/mnp.txid"

#define MONERO_ORANGE   "\033[38;2;255;102;0m"
#define MONERO_GREEN    "\033[38;2;50;205;50m"
#define MONERO_GREY     "\033[38;2;76;76;76m"
#define ANSI_RESET_ALL  "\033[0m"

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

#define SPEND_PROOF_CMD "check_spend_proof"
#define TX_PROOF_CMD    "check_tx_proof"

#define TRANSACTION_DIR "transactions"

#define PAYNULL         "0000000000000000"
#define NOPARAMS        NULL
#define CONTENT_TYPE    "Content-Type: application/json"
#define JSON_RPC        "2.0"
#define SLEEPTIME       (5)
#endif
