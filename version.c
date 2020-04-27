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

#include <stdlib.h>
#include <stdio.h>
#include "./cjson/cJSON.h"
#include "out_version.h"
#include "globaldefs.h"

int out_version(cJSON **version) {
    
    int ret = 0;
    const cJSON *result = NULL;
    result = cJSON_GetObjectItem(*version, "result");
    unsigned int v = cJSON_GetObjectItem(result, "version")->valueint;
    unsigned int major = (v & MAJOR_MASK) >> 16;
    unsigned int minor = (v & MINOR_MASK);
    char *res = cJSON_Print(result);
    if (verbose) fprintf(stdout, "rpc version: v%d.%d.\n", major, minor);

    return 0;
}

