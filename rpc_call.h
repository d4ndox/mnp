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
       /* tx related */
       char *txid;
       char *payid;
       char *saddr;
       char *iaddr;
       char *amount;
       char *conf;
       char *fifo;
       int   idx;
       cJSON *reply;
};

int rpc_call(struct rpc_wallet *monero_wallet);
char* get_method(enum monero_rpc_method method);

#endif
