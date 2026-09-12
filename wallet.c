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

#include "wallet.h"

#include <curl/curl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globaldefs.h"

static int is_transfer_request(const char *cmd);
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp);

/**
 * Performs an HTTP POST request to monero-wallet-rpc.
 *
 * Transfer requests use a longer timeout because hardware wallets may require
 * interactive confirmation before monero-wallet-rpc can return.
 *
 * @param urlport The complete wallet RPC endpoint URL.
 * @param cmd The JSON-RPC request body.
 * @param userpwd The RPC username and password in user:password format.
 * @param answer A pointer receiving the dynamically allocated RPC response.
 * @return The response size on success, or -1 if the request fails.
 */
int wallet(const char *urlport, const char *cmd, const char *userpwd, char **answer)
{
    CURL *curl_handle = NULL;
    CURLcode curl_result;
    struct curl_slist *headers = NULL;
    struct MemoryStruct chunk = {0};
    long timeout;
    int result = -1;

    if (urlport == NULL ||
        cmd == NULL ||
        userpwd == NULL ||
        answer == NULL) {
        return -1;
    }

    *answer = NULL;
    timeout = is_transfer_request(cmd) ? TRANSFER_TIMEOUT : RES_TIMEOUT;

    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        fprintf(stderr, "mnp: curl global initialization failed\n");
        return -1;
    }

    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "mnp: curl initialization failed\n");
        goto done;
    }

    headers = curl_slist_append(headers, CONTENT_TYPE);

    if (headers == NULL) {
        fprintf(stderr, "mnp: cannot allocate curl HTTP headers\n");
        goto done;
    }

    chunk.memory = malloc(1);

    if (chunk.memory == NULL) {
        fprintf(stderr, "mnp: cannot allocate RPC response buffer\n");
        goto done;
    }

    chunk.memory[0] = '\0';
    chunk.size = 0;

    if (curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_URL, urlport) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long)strlen(cmd)) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, cmd) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_USERPWD, userpwd) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, (long)CURLAUTH_DIGEST) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST") != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_USE_SSL, CURLUSESSL_TRY) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, timeout) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, CONNECTTIMEOUT) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk) != CURLE_OK ||
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "mnp/" VERSION) != CURLE_OK) {
        fprintf(stderr, "mnp: cannot configure curl request\n");
        goto done;
    }

    curl_result = curl_easy_perform(curl_handle);

    if (curl_result != CURLE_OK &&
        curl_result != CURLE_GOT_NOTHING) {
        fprintf(
            stderr,
            "mnp: curl request failed: %s\n",
            curl_easy_strerror(curl_result)
        );
        goto done;
    }

    if (chunk.size > INT_MAX) {
        fprintf(stderr, "mnp: RPC response is too large\n");
        goto done;
    }

    *answer = chunk.memory;
    chunk.memory = NULL;
    result = (int)chunk.size;

done:
    free(chunk.memory);
    curl_slist_free_all(headers);

    if (curl_handle != NULL) {
        curl_easy_cleanup(curl_handle);
    }

    curl_global_cleanup();

    return result;
}

/**
 * Determines whether an RPC request may require interactive transaction signing.
 *
 * @param cmd The JSON-RPC request body.
 * @return Non-zero for transfer requests, otherwise zero.
 */
static int is_transfer_request(const char *cmd)
{
    if (cmd == NULL) {
        return 0;
    }

    return strstr(cmd, "\"method\":\"transfer\"") != NULL ||
           strstr(cmd, "\"method\": \"transfer\"") != NULL ||
           strstr(cmd, "\"method\":\"transfer_split\"") != NULL ||
           strstr(cmd, "\"method\": \"transfer_split\"") != NULL;
}

/**
 * Appends received HTTP response data to the RPC response buffer.
 *
 * @param contents A pointer to the received response data.
 * @param size The size of each received element.
 * @param nmemb The number of received elements.
 * @param userp A pointer to the MemoryStruct receiving the response.
 * @return The number of bytes written, or 0 if memory allocation fails.
 */
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct MemoryStruct *memory;
    char *new_memory;
    size_t real_size;

    if (contents == NULL || userp == NULL) {
        return 0;
    }

    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return 0;
    }

    real_size = size * nmemb;
    memory = userp;

    if (memory->size > SIZE_MAX - real_size - 1) {
        return 0;
    }

    new_memory = realloc(
        memory->memory,
        memory->size + real_size + 1
    );

    if (new_memory == NULL) {
        fprintf(stderr, "mnp: cannot grow RPC response buffer\n");
        return 0;
    }

    memory->memory = new_memory;

    memcpy(
        memory->memory + memory->size,
        contents,
        real_size
    );

    memory->size += real_size;
    memory->memory[memory->size] = '\0';

    return real_size;
}
