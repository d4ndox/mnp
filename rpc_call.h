#ifndef RPC_CALL_H
#define RPC_CALL_H

#include "./cjson/cJSON.h"

enum monero_rpc_method {
    GET_HEIGHT,
    GET_BALANCE,
    GET_TXID,
    GET_LIST,
    GET_SUBADDR,
    NEW_SUBADDR,
    MK_IADDR,
    MK_URI,
    SPLIT_IADDR,
    CHECK_SPEND_PROOF,
    CHECK_TX_PROOF,
    SIGN_MESSAGE,
    VERIFY_MESSAGE,
    GET_TRANSFERS,
    PARSE_URI,
    TRANSFER,
    END_RPC_SIZE
};

struct rpc_wallet {
       int monero_rpc_method;
       char *params;
       char *account;
       char *host;
       char *port;
       char *user;
       char *pwd;
       /* mnpd related */
       char *balance;
       char *height;
       char *file;
       /* mnp usage */
       char *txid;
       char *payid;
       char *saddr;
       char *iaddr;
       char *amount;
       char *conf;
       char *locked;
       char *fifo;
       /* mnp proof */
       char *message;
       char *signature;
       char *proof;
       /*mnp sign/verify */
       char *data;
       /*mnp transactions */
       int transactions_in;
       int transactions_out;
       int transactions_pending;
       int transactions_failed;
       int transactions_pool;
       /* general */
       int   idx;
       cJSON *reply;
};

int rpc_call(struct rpc_wallet *monero_wallet);
char* get_method(enum monero_rpc_method method);

#endif
