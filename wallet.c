/*
 * Monero Named Pipes (mnp) is a Unix Monero wallet client.
 * Copyright (C) 2019 https://github.com/nonie-sys/mnp
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

#include <curl/curl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "wallet.h"
#include "globaldefs.h"

int wallet(const char *urlport, const char *cmd, char *userpwd, char **answer) {
                
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
    struct curl_httppost *post;
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl_handle, CURLOPT_USE_SSL, CURLUSESSL_TRY);

    if (DEBUG) fprintf(stderr, "%s\n", urlport);
    if (DEBUG) fprintf(stderr, "%s\n", userpwd);
    if (DEBUG) fprintf(stderr, "%s\n", cmd);
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
