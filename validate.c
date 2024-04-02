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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include "globaldefs.h"


/*
 * Validates hexadecimal input of specified size.
 * Checks for valid payment ID, transaction ID, subaddress, or integrated address.
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

    /* Check if input length matches specified size */
    if (strlen(hex) != size) {
        return -1;
    }

    /* Check if each character is a valid hexadecimal digit */
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
    /* Check if the input contains only digits */
    for (int i = 0; i < strlen(amount); i++) {
        if (!isdigit(amount[i])) {
            return -1;
        }
    }

    return 0;
}
