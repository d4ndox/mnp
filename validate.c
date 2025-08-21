/*
 * Copyright (c) 2025 d4ndo@proton.me
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include "globaldefs.h"


/*
 * Validates hexadecimal input of specified size.
 * Checks for valid payment ID or transaction ID.
 *
 * Parameters:
 *   hex: Pointer to the hexadecimal input string
 *   size: Size of the input string to be validated
 *
 * Returns:
 *   -1 if input is invalid
 *    0 if input is valid
 */
int val_hex_input(const char *hex, const unsigned int size) {

    if (strlen(hex) != size) {
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (!isxdigit(hex[i])) {
            return -1;
        }
    }

    return 0;
}


/*
 * Validates an input string to ensure it contains only digits.
 *
 * Parameters:
 *   amount: Pointer to the input string to be validated
 *
 * Returns:
 *   -1 if input contains non-digit characters
 *    0 if input contains only digits
 */
int val_amount(const char *amount) {

    for (int i = 0; i < strlen(amount); i++) {
        if (!isdigit(amount[i])) {
            return -1;
        }
    }

    return 0;
}
