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
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "./cjson/cJSON.h"
#include "balance.h"
#include "globaldefs.h"
#include <unistd.h>


char* balance(cJSON **reply, char *balance_fifo, char *status_balance)
{
    pid_t pid;
    int ret = 0;
    int cmp = 0;
    int fd = 0;

    cJSON *result = cJSON_GetObjectItem(*reply, "result");
    cJSON *balance = cJSON_GetObjectItem(result, "balance");
   
    char *obalance = cJSON_Print(balance);
    char *dummy = NULL;

    if (status_balance == NULL) {
         status_balance = malloc(MAX_DATA_SIZE * sizeof(char));
         status_balance = strndup(obalance, sizeof(obalance));
    }

    cmp = strncmp(obalance, status_balance, MAX_DATA_SIZE);
    
    if(DEBUG) fprintf(stderr, "total-balance = %s\n", obalance);
    if (cmp == 0) {
        return status_balance;
    } else {
        fd = open(balance_fifo, O_NONBLOCK, O_RDONLY);
        ret = read(fd, dummy, MAX_DATA_SIZE);
        close(fd);
    }

    pid = fork();
    if (pid == -1)
    {
        return NULL;
    } 

    if (pid == 0 && cmp != 0)
    {
        fd = open(balance_fifo, O_WRONLY);
        ret = write(fd, obalance, strlen(obalance)+1);
        close(fd);
        exit(0);
    }

    if (pid > 0 && cmp != 0) {
           status_balance = strndup(obalance, MAX_DATA_SIZE);
           if (verbose) fprintf(stderr, "balance total: %s\n", obalance);
    }

    return status_balance;
}
