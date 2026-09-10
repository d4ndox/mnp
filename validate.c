/*
 * Copyright (c) 2026 d4ndo@proton.me
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
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globaldefs.h"

static int val_base58(const char *value);

/**
 * Validates hexadecimal input of specified size.
 *
 * @param hex Pointer to the hexadecimal input string.
 * @param size Expected input size.
 * @return 0 if valid, or -1 otherwise.
 */
int val_hex_input(const char *hex, const unsigned int size)
{
    unsigned int i;

    if (hex == NULL || strlen(hex) != size) {
        return -1;
    }

    for (i = 0; i < size; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return -1;
        }
    }

    return 0;
}

/**
 * Validates an atomic amount.
 *
 * @param amount Pointer to the amount string.
 * @return 0 if valid, or -1 otherwise.
 */
int val_amount(const char *amount)
{
    size_t i;

    if (amount == NULL || *amount == '\0') {
        return -1;
    }

    for (i = 0; i < strlen(amount); i++) {
        if (!isdigit((unsigned char)amount[i])) {
            return -1;
        }
    }

    return 0;
}

/**
 * Performs a lightweight offline validation of a Monero address.
 *
 * This validates only length and Base58 syntax. It does not verify
 * network type, checksum, or cryptographic validity.
 *
 * @param address Pointer to the address string.
 * @return 0 if plausible, or -1 otherwise.
 */
int val_address(const char *address)
{
    size_t length;

    if (address == NULL) {
        return -1;
    }

    length = strlen(address);

    if (length != 95 && length != 106) {
        return -1;
    }

    return val_base58(address);
}

/**
 * Performs a lightweight offline validation of a Monero message signature.
 *
 * Cryptographic validity is checked later by monero-wallet-rpc verify.
 *
 * @param signature Pointer to the signature string.
 * @return 0 if plausible, or -1 otherwise.
 */
int val_signature(const char *signature)
{
    size_t length;

    if (signature == NULL) {
        return -1;
    }

    length = strlen(signature);

    if (length < 6) {
        return -1;
    }

    if (strncmp(signature, "SigV", 4) != 0) {
        return -1;
    }

    if (!isdigit((unsigned char)signature[4])) {
        return -1;
    }

    return val_base58(signature + 5);
}

/**
 * Validates characters against the Monero Base58 alphabet.
 *
 * @param value Pointer to the string to validate.
 * @return 0 if valid, or -1 otherwise.
 */
static int val_base58(const char *value)
{
    static const char alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZ"
        "abcdefghijkmnopqrstuvwxyz";
    size_t i;

    if (value == NULL || *value == '\0') {
        return -1;
    }

    for (i = 0; value[i] != '\0'; i++) {
        if (strchr(alphabet, value[i]) == NULL) {
            return -1;
        }
    }

    return 0;
}
