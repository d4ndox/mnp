/*
 * Copyright (c) 2024 d4ndo@proton.me
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

#include <curl/curl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "wallet.h"
#include "globaldefs.h"

/* defined redundant because of static */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

int wallet(const char *urlport, const char *cmd, const char *userpwd, char **answer)
{
    CURL *curl_handle;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    struct curl_slist *headers = NULL;
    curl_slist_append(headers, CONTENT_TYPE);

    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_URL, urlport);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, (long) strlen(cmd));
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, cmd);
    curl_easy_setopt(curl_handle, CURLOPT_USERPWD, userpwd);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, (long)CURLAUTH_DIGEST);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl_handle, CURLOPT_USE_SSL, CURLUSESSL_TRY);

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);

    if(res == CURLE_GOT_NOTHING) {
        chunk.memory = realloc(chunk.memory, 2 * sizeof(char));
        chunk.memory = strndup("\0", 2);
        chunk.size = 0;
    }

    if(res != CURLE_OK && res != CURLE_GOT_NOTHING) {
        fprintf(stderr, "curl error num %d\n", res);
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                        curl_easy_strerror(res));
        return -1;
    }

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    *answer = chunk.memory;
    return chunk.size;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
