#ifndef RPC_CALL_H
#define RPC_CALL_H

#include "./cjson/cJSON.h"

int rpc_call(const char *method, const char *params, const char *urlport, const char *userpwd, cJSON **reply);

#endif
