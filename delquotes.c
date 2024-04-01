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

#include <string.h>
#include <stdlib.h>
#include "delquotes.h"

/*
 * Delete Quotes
 * first and last character in string
 * is removed
 */
char* delQuotes(const char *str) {
    size_t length = strlen(str);

    /* Return empty string */
    if (length <= 2) {
        char* empty =  malloc(1 * sizeof(char));
        empty[0] = '\0';
        return empty;
    }

    char* ret = malloc((length - 1) * sizeof(char));
    ret = strndup(str + 1, length - 2);
    ret[length - 2] = '\0';

    return ret;
}
