/* Exercise confirmation polling with wallet RPC fixtures, without a wallet. */
#include "../monitor.c"
#include <assert.h>

static int calls;
static const char *first_transfer;

int rpc_call(struct rpc_wallet *wallet)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *transfers = cJSON_CreateArray();
    assert(++calls <= 2);
    wallet->reply = cJSON_CreateObject();
    cJSON_AddItemToObject(wallet->reply, "result", result);
    cJSON_AddItemToObject(result, "transfers", transfers);
    cJSON_AddItemToArray(transfers, cJSON_Parse(calls == 1 ? first_transfer :
        "{\"type\":\"in\",\"confirmations\":1}"));
    return 0;
}

int main(void)
{
    const char *waiting[] = {
        "{\"type\":\"pool\",\"height\":0}",
        "{\"type\":\"pool\",\"confirmations\":0}"
    };
    const char *invalid[] = {
        "{\"confirmations\":null}", "{\"confirmations\":\"1\"}"
    };
    for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); ++i) {
        struct rpc_wallet wallet = {0};
        cJSON *transfers = NULL;
        calls = 0;
        first_transfer = waiting[i];
        assert(poll_transaction(&wallet, CONFIRMED, 1, 0, "/unused", &transfers) == 0);
        assert(calls == 2); /* Never notify before the confirmation arrives. */
        cJSON_Delete(wallet.reply);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        struct rpc_wallet wallet = {0};
        cJSON *transfers = NULL;
        calls = 0;
        first_transfer = invalid[i];
        assert(poll_transaction(&wallet, CONFIRMED, 1, 0, "/unused", &transfers) == -1);
        assert(calls == 1);
        cJSON_Delete(wallet.reply);
    }
    puts("Confirmation polling regression tests passed");
    return 0;
}
