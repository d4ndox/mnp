#ifndef BALANCE_H
#define BALANCE_H

#include "./cjson/cJSON.h"

char* balance(cJSON **reply, char *balance_fifo, char *status_balance);

#endif
