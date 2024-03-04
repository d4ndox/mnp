#ifndef RPC_CALL_H
#define RPC_CALL_H

#include "./cjson/cJSON.h"

struct payment {
    char *payid;
    char *iaddr;
    char *amount;
    char *payment_fifo;
};

enum monero_rpc_method {
    GET_HEIGHT,
    GET_BALANCE,
    GET_BULK_PAYMENTS,
    GET_LIST,
    GET_SUBADDR,
    MK_IADDR,
    SPLIT_IADDR,
    GET_VERSION,
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
       char *payid;
       char *saddr;
       char *iaddr;
       int   idx;
       struct payment *paymentlist;
       int    plsize;
       cJSON *reply;
};

int rpc_call(struct rpc_wallet *monero_wallet);
char* get_method(enum monero_rpc_method method);

#endif
