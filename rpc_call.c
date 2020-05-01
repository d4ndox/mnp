/*
 * Monero Named Pipes (mnp) is a Unix Monero wallet client.
 * Copyright (C) 2019-2020
 * http://mnp4i54qnixz336alilggo4zkxgf35oj7kodkli2as4q6gpvvy5lxrad.onion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include "./cjson/cJSON.h"
#include "wallet.h"
#include "rpc_call.h"
#include "globaldefs.h"

int rpc_call(const char *method, const char *params, const char *urlport, const char *userpwd, cJSON **rpc_reply)
{
    int ret = 0;

    /*
     * Pack the method string into a JSON frame for rpc call.
     */
    cJSON *rpc_frame = cJSON_CreateObject();

    if (cJSON_AddStringToObject(rpc_frame, "jsonrpc", JSON_RPC) == NULL) ret = -1;
    if (cJSON_AddStringToObject(rpc_frame, "id", "0") == NULL) ret = -1;
    if (cJSON_AddStringToObject(rpc_frame, "method", method) == NULL) ret = -1;

    char *method_call = cJSON_Print(rpc_frame);

    if (method_call == NULL) ret = -1;

    /*
     * rpc method call send to the wallet
     */
    char *reply = NULL;
    if (0 > (ret = wallet(urlport, method_call, userpwd, &reply))) {
        fprintf(stderr, "could not connect to host: %s\n", urlport);
        ret = -1;
    }

    /*
     * rpc_reply is the return value of this function
     * testing for errors while parseing the JSON string.
     */
    *rpc_reply = cJSON_Parse(reply);
    if (rpc_reply == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
            ret = -1;
        }
    }

    /*
     * Check for error code returned from wallet(rpc) call
     * test rpc_reply for any error codes.
     */
    cJSON *error = cJSON_GetObjectItemCaseSensitive(*rpc_reply, "error");
    const cJSON *mesg = cJSON_GetObjectItemCaseSensitive(error, "message");

    if (error != NULL && mesg->valuestring != NULL) {
        fprintf(stderr, "error message rpc: %s\n", mesg->valuestring);
        ret = -1;
    }

    if (verbose) fprintf(stdout, "%d bytes received\n", ret);

    cJSON_Delete(rpc_frame);
    cJSON_Delete(error);
    free(method_call);
    free(reply);
    return ret;
}
