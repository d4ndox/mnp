#ifndef VALIDATE_H
#define VALIDATE_H

int val_hex_input(const char *hex, const unsigned int size);
int val_amount(const char *amount);
int val_address(const char *address);
int val_signature(const char *signature);

#endif

