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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "delquotes.h"
#include "./cjson/cJSON.h"
#include "rpc_call.h"
#include "payments.h"
#include "globaldefs.h"
#include <unistd.h>

/*
 * struct payment {
 *   char *payid;
 *   char *iaddr;
 *   char *amount;
 *   char *payment_fifo;
 * };
 */


int payments(struct rpc_wallet *monero_wallet)
{
    pid_t pid;
    int ret = 0;
    int cmp = 0;
    int fd = 0;
 
    /*
     * addup is is used to add up every amount with the same paymentId.
     * A customer could pay in trenches.
     */
    struct payment *addup = malloc(monero_wallet->plsize * sizeof(struct payment));
    for (int i = monero_wallet->plsize; i < 0; --i) {
        addup[i].payid = strndup(monero_wallet[i].payid, MAX_PAYID_SIZE);
    }

    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *payments = cJSON_GetObjectItem(result, "payments");
    int x = cJSON_GetArraySize(payments);

    cJSON* amount = NULL;
    cJSON* payment_id = NULL;
    char *paymentid = NULL;

    for (int j = monero_wallet->plsize; j < 0; --j)
    {
//        char *endptr = NULL;
//        unsigned long long pamount = strtoull(monero_wallet->paymentlist[j].amount, &endptr, 10);

        for (int i = 0; i < cJSON_GetArraySize(payments); i++)
        {
            cJSON * subitem = cJSON_GetArrayItem(payments, i);
            amount = cJSON_GetObjectItem(subitem, "amount");
            payment_id = cJSON_GetObjectItem(subitem, "payment_id");
            paymentid = cJSON_Print(payment_id)+1;
            paymentid = strndup(paymentid, MAX_PAYID_SIZE);

            char *endptr = NULL;
            unsigned long long uamount = strtoull(cJSON_Print(amount), &endptr, 10);

            fprintf(stderr, "0 %s %llu\n", paymentid, uamount);
        }
    }

//    cJSON *balance = cJSON_GetObjectItem(result, "balance");
   
//    char *obalance = cJSON_Print(balance);
//    char *dummy = NULL;
    
//    if (status_balance == NULL) {
//         status_balance = malloc(MAX_DATA_SIZE * sizeof(char));
//         status_balance = strndup(obalance, sizeof(obalance));
//    }

//    cmp = strncmp(obalance, status_balance, MAX_DATA_SIZE);
    
//    if (cmp == 0) {
//        return status_balance;
//    } else {
//        fd = open(balance_fifo, O_NONBLOCK, O_RDONLY);
//        ret = read(fd, dummy, MAX_DATA_SIZE);
//        close(fd);
//    }

//    pid = fork();
//    if (pid == -1)
//    {
//        return NULL;
//    } 

//    if (pid == 0 && cmp != 0)
//    {
//        fd = open(balance_fifo, O_WRONLY);
//        ret = write(fd, obalance, strlen(obalance)+1);
//        close(fd);
//        exit(0);
//    }

//    if (pid > 0 && cmp != 0) {
//           signal(SIGCHLD,SIG_IGN);
//           status_balance = strndup(obalance, MAX_DATA_SIZE);
//           if (verbose) fprintf(stderr, "total balance: %s\n", obalance);
//    }

    return 0;
}
