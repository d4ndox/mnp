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

#include <string.h>
#include <stdlib.h>
#include "delquotes.h"

/*
 * Deletes the first and last quotation mark from a string.
 *
 * Parameters:
 *   str: Pointer to the input string
 *
 * Returns:
 *   char: pointer to new string without quotation marks.
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
