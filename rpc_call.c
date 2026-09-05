/*
 * Copyright (c) 2026 d4ndo@proton.me
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "rpc_call.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "wallet.h"

static int add_rpc_parameters(cJSON *params, const struct rpc_wallet *monero_wallet);
static int add_subaddress_index(cJSON *params, int index);

/**
 * Calls the configured JSON-RPC method on monero-wallet-rpc.
 *
 * The generated JSON response is stored in monero_wallet->reply. Any previous
 * response stored in the structure is released before the new request.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the request data.
 * @return 0 on success, or -1 if the request, response parsing, or RPC operation fails.
 */
int rpc_call(struct rpc_wallet *monero_wallet)
{
    char *urlport = NULL;
    char *userpwd = NULL;
    char *method = NULL;
    char *method_call = NULL;
    char *reply = NULL;
    cJSON *rpc_params = NULL;
    cJSON *rpc_frame = NULL;
    cJSON *error;
    cJSON *message;
    int response_size;
    int result = -1;

    if (monero_wallet == NULL) {
        return -1;
    }

    openlog("mnp:rpc_call:", LOG_PID, LOG_USER);

    if (monero_wallet->reply != NULL) {
        cJSON_Delete(monero_wallet->reply);
        monero_wallet->reply = NULL;
    }

    if (monero_wallet->host == NULL ||
        monero_wallet->port == NULL) {
        syslog(
            LOG_USER | LOG_ERR,
            "rpc_host and/or rpc_port is missing"
        );
        goto done;
    }

    if (monero_wallet->user == NULL ||
        monero_wallet->pwd == NULL) {
        syslog(
            LOG_USER | LOG_ERR,
            "rpc_user and/or rpc_password is missing"
        );
        goto done;
    }

    if (asprintf(
            &urlport,
            "http://%s:%s/json_rpc",
            monero_wallet->host,
            monero_wallet->port
        ) == -1) {
        urlport = NULL;
        goto done;
    }

    if (asprintf(
            &userpwd,
            "%s:%s",
            monero_wallet->user,
            monero_wallet->pwd
        ) == -1) {
        userpwd = NULL;
        goto done;
    }

    method = get_method(monero_wallet->monero_rpc_method);

    if (method == NULL) {
        syslog(
            LOG_USER | LOG_ERR,
            "invalid Monero RPC method: %d",
            monero_wallet->monero_rpc_method
        );
        goto done;
    }

    if (monero_wallet->monero_rpc_method != GET_HEIGHT) {
        rpc_params = cJSON_CreateObject();

        if (rpc_params == NULL) {
            goto done;
        }

        if (add_rpc_parameters(rpc_params, monero_wallet) == -1) {
            goto done;
        }
    }

    rpc_frame = cJSON_CreateObject();

    if (rpc_frame == NULL) {
        goto done;
    }

    if (cJSON_AddStringToObject(
            rpc_frame,
            "jsonrpc",
            JSON_RPC
        ) == NULL ||
        cJSON_AddStringToObject(
            rpc_frame,
            "id",
            "0"
        ) == NULL ||
        cJSON_AddStringToObject(
            rpc_frame,
            "method",
            method
        ) == NULL) {
        goto done;
    }

    if (rpc_params != NULL) {
        if (!cJSON_AddItemToObject(
                rpc_frame,
                "params",
                rpc_params
            )) {
            goto done;
        }

        rpc_params = NULL;
    }

    method_call = cJSON_PrintUnformatted(rpc_frame);

    if (method_call == NULL) {
        goto done;
    }

    response_size = wallet(
        urlport,
        method_call,
        userpwd,
        &reply
    );

    if (response_size < 0) {
        syslog(
            LOG_USER | LOG_ERR,
            "could not connect to host: %s",
            urlport
        );
        goto done;
    }

    if (DEBUG) {
        syslog(
            LOG_USER | LOG_DEBUG,
            "%d bytes received",
            response_size
        );
    }

    if (reply == NULL || reply[0] == '\0') {
        syslog(
            LOG_USER | LOG_ERR,
            "empty wallet RPC response"
        );
        goto done;
    }

    monero_wallet->reply = cJSON_Parse(reply);

    if (monero_wallet->reply == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();

        if (error_ptr != NULL) {
            syslog(
                LOG_USER | LOG_ERR,
                "invalid wallet RPC JSON near: %.80s",
                error_ptr
            );
        } else {
            syslog(
                LOG_USER | LOG_ERR,
                "invalid wallet RPC JSON"
            );
        }

        goto done;
    }

    error = cJSON_GetObjectItemCaseSensitive(
        monero_wallet->reply,
        "error"
    );

    if (error != NULL) {
        message = cJSON_GetObjectItemCaseSensitive(
            error,
            "message"
        );

        if (message != NULL &&
            cJSON_IsString(message) &&
            message->valuestring != NULL) {
            syslog(
                LOG_USER | LOG_ERR,
                "wallet RPC error: %s",
                message->valuestring
            );
        } else {
            syslog(
                LOG_USER | LOG_ERR,
                "wallet RPC returned an error"
            );
        }

        goto done;
    }

    result = 0;

done:
    if (result != 0 &&
        monero_wallet->reply != NULL) {
        cJSON_Delete(monero_wallet->reply);
        monero_wallet->reply = NULL;
    }

    cJSON_Delete(rpc_params);
    cJSON_Delete(rpc_frame);

    free(reply);
    free(method_call);
    free(method);
    free(userpwd);
    free(urlport);

    closelog();

    return result;
}

/**
 * Returns the JSON-RPC method name for a Monero RPC method.
 *
 * @param method The internal Monero RPC method identifier.
 * @return A dynamically allocated method name, or NULL if the method is unknown.
 */
char *get_method(enum monero_rpc_method method)
{
    const char *name;

    switch (method) {
    case GET_HEIGHT:
        name = GET_HEIGHT_CMD;
        break;

    case GET_BALANCE:
        name = GET_BALANCE_CMD;
        break;

    case GET_TXID:
        name = GET_TXID_CMD;
        break;

    case GET_LIST:
    case GET_SUBADDR:
        name = GET_SUBADDR_CMD;
        break;

    case NEW_SUBADDR:
        name = NEW_SUBADDR_CMD;
        break;

    case MK_IADDR:
        name = MK_IADDR_CMD;
        break;

    case MK_URI:
        name = MK_URI_CMD;
        break;

    case SPLIT_IADDR:
        name = SP_IADDR_CMD;
        break;

    case CHECK_SPEND_PROOF:
        name = SPEND_PROOF_CMD;
        break;

    case CHECK_TX_PROOF:
        name = TX_PROOF_CMD;
        break;

    default:
        return NULL;
    }

    return strdup(name);
}

/**
 * Adds method-specific parameters to a Monero wallet RPC request.
 *
 * @param params A pointer to the JSON object receiving the RPC parameters.
 * @param monero_wallet A pointer to the rpc_wallet structure containing request values.
 * @return 0 on success, or -1 if a required value is missing or JSON construction fails.
 */
static int add_rpc_parameters(cJSON *params, const struct rpc_wallet *monero_wallet)
{
    int account_index;

    if (params == NULL || monero_wallet == NULL) {
        return -1;
    }

    account_index = monero_wallet->account != NULL
        ? atoi(monero_wallet->account)
        : 0;

    switch (monero_wallet->monero_rpc_method) {
    case GET_BALANCE:
    case GET_LIST:
    case NEW_SUBADDR:
        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL) {
            return -1;
        }
        break;

    case GET_SUBADDR:
        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL) {
            return -1;
        }

        if (add_subaddress_index(
                params,
                monero_wallet->idx
            ) == -1) {
            return -1;
        }
        break;

    case MK_IADDR:
        if (monero_wallet->payid == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "payment_id",
                monero_wallet->payid
            ) == NULL) {
            return -1;
        }
        break;

    case MK_URI:
        if (monero_wallet->saddr == NULL ||
            monero_wallet->amount == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "address",
                monero_wallet->saddr
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "amount",
                monero_wallet->amount
            ) == NULL) {
            return -1;
        }
        break;

    case SPLIT_IADDR:
        if (monero_wallet->iaddr == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "integrated_address",
                monero_wallet->iaddr
            ) == NULL) {
            return -1;
        }
        break;

    case GET_TXID:
        if (monero_wallet->txid == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "txid",
                monero_wallet->txid
            ) == NULL) {
            return -1;
        }
        break;

    case CHECK_SPEND_PROOF:
        if (monero_wallet->txid == NULL ||
            monero_wallet->signature == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "txid",
                monero_wallet->txid
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "signature",
                monero_wallet->signature
            ) == NULL) {
            return -1;
        }

        if (monero_wallet->message != NULL &&
            cJSON_AddStringToObject(
                params,
                "message",
                monero_wallet->message
            ) == NULL) {
            return -1;
        }
        break;

    case CHECK_TX_PROOF:
        if (monero_wallet->txid == NULL ||
            monero_wallet->saddr == NULL ||
            monero_wallet->signature == NULL) {
            return -1;
        }

        if (cJSON_AddNumberToObject(
                params,
                "account_index",
                account_index
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "txid",
                monero_wallet->txid
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "address",
                monero_wallet->saddr
            ) == NULL ||
            cJSON_AddStringToObject(
                params,
                "signature",
                monero_wallet->signature
            ) == NULL) {
            return -1;
        }

        if (monero_wallet->message != NULL &&
            cJSON_AddStringToObject(
                params,
                "message",
                monero_wallet->message
            ) == NULL) {
            return -1;
        }
        break;

    default:
        return -1;
    }

    return 0;
}

/**
 * Adds a single subaddress index to an RPC parameter object.
 *
 * @param params A pointer to the JSON object receiving the address_index array.
 * @param index The subaddress index to add.
 * @return 0 on success, or -1 if JSON allocation fails.
 */
static int add_subaddress_index(cJSON *params, int index)
{
    cJSON *indices;
    cJSON *index_item;

    if (params == NULL || index < 0) {
        return -1;
    }

    indices = cJSON_CreateArray();

    if (indices == NULL) {
        return -1;
    }

    index_item = cJSON_CreateNumber(index);

    if (index_item == NULL) {
        cJSON_Delete(indices);
        return -1;
    }

    if (!cJSON_AddItemToArray(indices, index_item)) {
        cJSON_Delete(index_item);
        cJSON_Delete(indices);
        return -1;
    }

    if (!cJSON_AddItemToObject(
            params,
            "address_index",
            indices
        )) {
        cJSON_Delete(indices);
        return -1;
    }

    return 0;
}
