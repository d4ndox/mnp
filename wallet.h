#ifndef WALLET_H
#define WALLET_H

/* defined redundant because of static */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
int wallet(const char *urlport, const char *cmd, char *userpwd, char **answer);

#endif
