/* Monero Named Pipes (mnp) is a Unix Monero payment processor.
 * Copyright (C) 2019-2024
 * https://github.com/d4ndox/mnp
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "./cjson/cJSON.h"
#include "wallet.h"
#include "rpc_call.h"
#include "globaldefs.h"


int rpc_call(struct rpc_wallet *monero_wallet)
{
    int ret = 0;
    char *urlport = NULL;

    openlog("mnp:rpc_call:", LOG_PID, LOG_USER);

    if (monero_wallet->host != NULL && monero_wallet->port != NULL) {
        asprintf(&urlport,"http://%s:%s/json_rpc", monero_wallet->host, monero_wallet->port);
    } else {
        syslog(LOG_USER | LOG_ERR, "rpc_host and/or rpc_port is missing\n");
        ret = -1;
    }

    /* prepare rpc_user and rpc_password to connect to the wallet */
    char *userpwd = NULL;

    if (monero_wallet->user != NULL && monero_wallet->pwd != NULL) {
        asprintf(&userpwd,"%s:%s", monero_wallet->user, monero_wallet->pwd);
    } else {
        syslog(LOG_USER | LOG_ERR, "rpc_user and/or rpc_password is missing\n");
        ret = -1;
    }

     /*
     * Pack the method string into a JSON frame for rpc call.
     */
    char *method = get_method(monero_wallet->monero_rpc_method);

    /*
     * Pack the param string into a JSON frame for rpc call
     * accoring method used
     */
    cJSON *rpc_params = cJSON_CreateObject();

    switch (monero_wallet->monero_rpc_method) {
        case GET_VERSION:
            rpc_params = NULL;
            break;
        case GET_HEIGHT:
            rpc_params = NULL;
            break;
        case GET_BALANCE:
            if (cJSON_AddNumberToObject(rpc_params, "account_index",
                        atoi(monero_wallet->account)) == NULL) ret = -1;
            break;
        case GET_LIST:
            if (cJSON_AddNumberToObject(rpc_params, "account_index",
                        atoi(monero_wallet->account)) == NULL) ret = -1;
           break;
        case GET_SUBADDR:
            if (cJSON_AddNumberToObject(rpc_params, "account_index",
                        atoi(monero_wallet->account)) == NULL) ret = -1;
            cJSON *subarray = cJSON_CreateArray();
            cJSON *index = cJSON_CreateNumber(monero_wallet->idx);
            cJSON_AddItemToArray(subarray, index);
            cJSON_AddItemToObject(rpc_params, "address_index", subarray);
            break;
        case NEW_SUBADDR:
            if (cJSON_AddNumberToObject(rpc_params, "account_index",
                        atoi(monero_wallet->account)) == NULL) ret = -1;
            break;
        case MK_IADDR:
              if (cJSON_AddNumberToObject(rpc_params, "account_index",
                          atoi(monero_wallet->account)) == NULL) ret = -1;
              if (cJSON_AddStringToObject(rpc_params, "payment_id",
                          monero_wallet->payid) == NULL) ret = -1;
            break;
        case MK_URI:
              if (cJSON_AddNumberToObject(rpc_params, "account_index",
                          atoi(monero_wallet->account)) == NULL) ret = -1;
              if (cJSON_AddStringToObject(rpc_params, "address",
                              monero_wallet->saddr) == NULL) ret = -1;
              if (cJSON_AddStringToObject(rpc_params, "amount",
                              monero_wallet->amount) == NULL) ret = -1;
            break;
        case SPLIT_IADDR:
              if (cJSON_AddNumberToObject(rpc_params, "account_index",
                          atoi(monero_wallet->account)) == NULL) ret = -1;
              if (cJSON_AddStringToObject(rpc_params, "integrated_address",
                          monero_wallet->iaddr) == NULL) ret = -1;
            break;
        case GET_TXID:
              if (cJSON_AddNumberToObject(rpc_params, "account_index",
                          atoi(monero_wallet->account)) == NULL) ret = -1;
              if (cJSON_AddStringToObject(rpc_params, "txid",
                          monero_wallet->txid) == NULL) ret = -1;
            break;
        default:
            rpc_params = NULL;
            break;
    }

    cJSON *rpc_frame = cJSON_CreateObject();
    if (cJSON_AddStringToObject(rpc_frame, "jsonrpc", JSON_RPC) == NULL) ret = -1;
    if (cJSON_AddStringToObject(rpc_frame, "id", "0") == NULL) ret = -1;
    if (cJSON_AddStringToObject(rpc_frame, "method", method) == NULL) ret = -1;
    if (rpc_params != NULL) cJSON_AddItemToObject(rpc_frame, "params", rpc_params);

    char *method_call = cJSON_Print(rpc_frame);
    if (method_call == NULL) ret = -1;
    if (monero_wallet->monero_rpc_method == GET_BALANCE) fprintf(stderr, "method = %s", method_call);
    /*
     * rpc method call send to the wallet
     */
    char *reply = NULL;
    if (0 > (ret = wallet(urlport, method_call, userpwd, &reply))) {
        syslog(LOG_USER | LOG_ERR, "could not connect to host: %s", urlport);
        ret = -1;
    }

    /*
     * rpc_reply is the return value of this function
     * testing for errors while parseing the JSON string.
     */
    monero_wallet->reply = cJSON_Parse(reply);
    if (monero_wallet->reply == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            syslog(LOG_USER | LOG_ERR, "error before: %s", error_ptr);
            ret = -1;
        }
    }

    if (DEBUG) {
        syslog(LOG_USER | LOG_DEBUG, "%d bytes received", ret);
    }

    /*
     * Check for error code returned from wallet(rpc) call
     * test rpc_reply for any error codes.
     */
    cJSON *error = cJSON_GetObjectItemCaseSensitive(monero_wallet->reply, "error");
    const cJSON *mesg = cJSON_GetObjectItemCaseSensitive(error, "message");

    if (error != NULL && mesg->valuestring != NULL) {
        syslog(LOG_USER | LOG_ERR, "error message rpc: %s", mesg->valuestring);
        ret = -1;
    }

    cJSON_Delete(rpc_frame);
    cJSON_Delete(error);
    free(method);
    free(method_call);
    closelog();
    return ret;
}


char* get_method(enum monero_rpc_method method)
{
    char *mtd = NULL;

    switch (method) {
        case GET_VERSION:
            asprintf(&mtd, "%s", GET_VERSION_CMD);
                break;
        case GET_HEIGHT:
            asprintf(&mtd, "%s", GET_HEIGHT_CMD);
                break;
        case GET_BALANCE:
            asprintf(&mtd, "%s", GET_BALANCE_CMD);
                break;
        case GET_LIST:
            asprintf(&mtd, "%s", GET_SUBADDR_CMD);
                break;
        case GET_SUBADDR:
            asprintf(&mtd, "%s", GET_SUBADDR_CMD);
                break;
        case NEW_SUBADDR:
            asprintf(&mtd, "%s", NEW_SUBADDR_CMD);
                break;
        case MK_IADDR:
            asprintf(&mtd, "%s", MK_IADDR_CMD);
                break;
        case MK_URI:
            asprintf(&mtd, "%s", MK_URI_CMD);
                break;
        case SPLIT_IADDR:
            asprintf(&mtd, "%s", SP_IADDR_CMD);
                break;
        case GET_TXID:
            asprintf(&mtd, "%s", GET_TXID_CMD);
                break;
        default:
                break;
    }
    return mtd;
}
