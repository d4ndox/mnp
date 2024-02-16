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
#include "bc_height.h"
#include "globaldefs.h"
#include <unistd.h>

char* blockchainheight(cJSON **reply, char *bc_height_fifo, char *status_bc_height)
{
    pid_t pid;
    int ret = 0;
    int cmp = 0;
    int fd = 0;
    FILE *fp = NULL;

    cJSON *result = cJSON_GetObjectItem(*reply, "result");
    cJSON *height = cJSON_GetObjectItem(result, "height");

    char *oheight = cJSON_Print(height);
    char *dummy = NULL;

    if (status_bc_height == NULL) {
         status_bc_height = malloc(MAX_DATA_SIZE * sizeof(char));
         status_bc_height = strndup(oheight, sizeof(oheight));
    }

    cmp = strncmp(oheight, status_bc_height, MAX_DATA_SIZE);

    if (cmp == 0) {
        return status_bc_height;
    } else {
        fd = open(bc_height_fifo, O_NONBLOCK, O_RDONLY);
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
        fd = open(bc_height_fifo, O_WRONLY);
        ret = write(fd, oheight, strlen(oheight)+1);
        close(fd);
        exit(0);
    }

    if (pid > 0 && cmp != 0) {
           status_bc_height = strndup(oheight, MAX_DATA_SIZE);
           if (verbose) fprintf(stderr, "bc_height increased: %s\n", oheight);
    }

    return status_bc_height;
}
