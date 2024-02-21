#ifndef RPC_CALL_H
#define RPC_CALL_H

#include "./cjson/cJSON.h"

enum monero_rpc_method {
    GET_VERSION,
    GET_HEIGHT,
    GET_BALANCE,
    GET_LIST,
    GET_SUBADDR,
    GET_PAYMENTID,
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
       char *payid;;
       int   idx;
       cJSON *reply;
};
 
//for ( int i = 0; i != END_RPC_SIZE; i++ )
//{
//   monero_rpc_method monero_method = static_cast<monero_rpc_type>(i);
//}


//int rpc_call2(const char *method, const char *params, const char *urlport, const char *userpwd, cJSON **reply);
int rpc_call(struct rpc_wallet *monero_wallet);
char* get_method(enum monero_rpc_method method);

#endif
