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
#include "version.h"
#include "globaldefs.h"

int version(cJSON **version)
{
    int ret = 0;
    cJSON *result = cJSON_GetObjectItem(*version, "result");

    unsigned int v = cJSON_GetObjectItem(result, "version")->valueint;
    unsigned int major = (v & MAJOR_MASK) >> 16;
    unsigned int minor = (v & MINOR_MASK);
    //if (verbose) fprintf(stdout, "rpc version: v%d.%d\n", major, minor);

    return 0;
}

