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

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdio.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include "delquotes.h"
#include "./inih/ini.h"
#include "./cjson/cJSON.h"
#include "rpc_call.h"
#include "version.h"
#include "validate.h"
#include "globaldefs.h"

static const struct option options[] = {
	{"help"         , no_argument      , NULL, 'h'},
        {"rpc_user"     , required_argument, NULL, 'u'},
        {"rpc_password" , required_argument, NULL, 'r'},
        {"rpc_host"     , required_argument, NULL, 'i'},
        {"rpc_port"     , required_argument, NULL, 'p'},
        {"account"      , required_argument, NULL, 'a'},
        {"amount"       , required_argument, NULL, 'x'},
        {"subaddr"      , required_argument, NULL, 's'},
        {"newaddr"      , no_argument      , NULL, 'n'},
        {"version"      , no_argument      , NULL, 'v'},
        {"list"         , no_argument      , NULL, 'l'},
	{NULL, 0, NULL, 0}
};

static int handler(void *user, const char *section,
                   const char *name, const char *value);
static char *optstring = "hu:r:i:p:a:s:nxvl";
static void usage(int status);
static void printmnp(void);
static char *readStdin(void);

int main(int argc, char **argv)
{
    /* variables are set by getopt and/or config parser handler()*/
    int opt, lindex = -1;
    char *rpc_user = NULL;
    char *rpc_password = NULL;
    char *rpc_host = NULL;
    char *rpc_port = NULL;
    char *account = NULL;
    char *amount = NULL;
    char *paymentId = NULL;
    int subaddr = -1;
    int list = 0;
    int new = 0;
    int ret = 0;

    /* prepare for reading the config ini file */
    const char *homedir;

    if ((homedir = getenv("home")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    char *ini = NULL;
    char *home = strndup(homedir, MAX_DATA_SIZE);
    asprintf(&ini, "%s/%s", homedir, CONFIG_FILE);
    free(home);

    /* parse config ini file */
    struct Config config;

    if (ini_parse(ini, handler, &config) < 0) {
        fprintf(stderr, "can't load %s. try make install.\n", ini);
        exit(EXIT_FAILURE);
    }
    free(ini);

   /* get command line options */
    while((opt = getopt_long(argc, argv, optstring, options, &lindex)) != -1) {
        switch(opt) {
            case 'h':
	        usage(EXIT_SUCCESS);
		exit(EXIT_SUCCESS);
                break;
	    case 'u':
                rpc_user = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'r':
                rpc_password = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'i':
                rpc_host = strndup(optarg, MAX_DATA_SIZE);
	    case 'p':
                rpc_port = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'a':
                account = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 's':
                subaddr = atoi(optarg);
                break;
            case 'v':
                printmnp();
                exit(EXIT_SUCCESS);
                break;
            case 'l':
                list = 1;
                break;
            case 'n':
                new = 1;
                break;
            case 'x':
                amount = strndup(optarg, MAX_DATA_SIZE);
                int val = val_amount(amount);
                if (val < 0) {
                    fprintf(stderr, "Invalid amount\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 0:
	        break;
            default:
	        break;
        }
    }

    /* if no command line option is set - use the config ini file */
    if (account == NULL) {
        account = strndup(config.mnp_account, MAX_DATA_SIZE);
    } if (rpc_user == NULL) {
        rpc_user = strndup(config.rpc_user, MAX_DATA_SIZE);
    } if (rpc_password == NULL) {
        rpc_password = strndup(config.rpc_password, MAX_DATA_SIZE);
    } if (rpc_host == NULL) {
        rpc_host = strndup(config.rpc_host, MAX_DATA_SIZE);
    } if (rpc_port == NULL) {
        rpc_port = strndup(config.rpc_port, MAX_DATA_SIZE);
    }

    if (!(list == 1 || (subaddr > 0) || (new == 1))) {
        if (optind < argc) {
            paymentId = (char *)argv[optind];
        }
        if (paymentId == NULL) {
            paymentId = readStdin();
        }
        int valid = val_hex_input(paymentId, MAX_PAYID_SIZE);
        if (valid < 0) {
            fprintf(stderr, "Invalid payment Id\n");
            exit(EXIT_FAILURE);
        }
    }

    struct rpc_wallet *monero_wallet = (struct rpc_wallet*)malloc(END_RPC_SIZE * sizeof(struct rpc_wallet));
    monero_wallet[0].monero_rpc_method = GET_VERSION;

    /* initialise monero_wallet with NULL */
    for (int i = 0; i < END_RPC_SIZE; i++) {
        monero_wallet[i].monero_rpc_method = i;
        monero_wallet[i].params = NULL;
        monero_wallet[i].account = NULL;
        monero_wallet[i].host = NULL;
        monero_wallet[i].port = NULL;
        monero_wallet[i].user = NULL;
        monero_wallet[i].pwd = NULL;
        monero_wallet[i].payid = NULL;
        monero_wallet[i].saddr = NULL;
        monero_wallet[i].idx = 0;
        monero_wallet[i].reply = NULL;
    }

    for (int i = 0; i < END_RPC_SIZE; i++) {

        if (account != NULL) {
            monero_wallet[i].account = strndup(account, MAX_DATA_SIZE);
        } else {
            monero_wallet[i].account = strndup("0", MAX_DATA_SIZE);
        }
        if (rpc_host != NULL) {
            monero_wallet[i].host = strndup(rpc_host, MAX_DATA_SIZE);
        } else {
            fprintf(stderr, "rpc_host is missing\n");
            exit(EXIT_FAILURE);
        }
        if (rpc_port != NULL) {
            monero_wallet[i].port = strndup(rpc_port, MAX_DATA_SIZE);
        } else {
            fprintf(stderr, "rpc_port is missing\n");
            exit(EXIT_FAILURE);
        }
        if (rpc_user != NULL) {
            monero_wallet[i].user = strndup(rpc_user, MAX_DATA_SIZE);
        } else {
            fprintf(stderr, "rpc_user is missing\n");
            exit(EXIT_FAILURE);
        }
        if (rpc_password != NULL) {
            monero_wallet[i].pwd = strndup(rpc_password, MAX_DATA_SIZE);
        } else {
            fprintf(stderr, "rpc_password is missing\n");
            exit(EXIT_FAILURE);
        }
    }

    /* if no account is set - use the default account 0 */
    if (account == NULL) asprintf(&account, "0");

    /* mnp-payment --list */
    if (list == 1) {
          if (0 > (ret = rpc_call(&monero_wallet[GET_LIST]))) {
              fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[GET_LIST].host,
                                                                    monero_wallet[GET_LIST].port);
              exit(EXIT_FAILURE);
          }

          cJSON *result = cJSON_GetObjectItem(monero_wallet[GET_LIST].reply, "result");
          cJSON *address = cJSON_GetObjectItem(result, "addresses");
          cJSON *idx = NULL;
          cJSON *adr = NULL;

          for (int i = 0; i < cJSON_GetArraySize(address); i++)
          {
              cJSON *subadr = cJSON_GetArrayItem(address, i);
              idx = cJSON_GetObjectItem(subadr, "address_index");
              adr = cJSON_GetObjectItem(subadr, "address");

              fprintf(stdout, "%s %s\n", cJSON_Print(idx), cJSON_Print(adr));
          }
    }

    /*
     * mnp-payment --subaddr 1
     * returns subaddress
     * mnp-payment --amount 10 --subaddr 1
     * returns uri with subaddress at idx 1 + amount
     */
    if (subaddr >= 0) {
          monero_wallet[GET_SUBADDR].idx = subaddr;

          if (0 > (ret = rpc_call(&monero_wallet[GET_SUBADDR]))) {
              fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[GET_SUBADDR].host,
                                                                    monero_wallet[GET_SUBADDR].port);
              exit(EXIT_FAILURE);
          }
          cJSON *result = cJSON_GetObjectItem(monero_wallet[GET_SUBADDR].reply, "result");
          cJSON *address = cJSON_GetObjectItem(result, "addresses");
          cJSON *subadr = cJSON_GetArrayItem(address, 0);
          cJSON *adr = cJSON_GetObjectItem(subadr, "address");
          char *retaddr = delQuotes(cJSON_Print(adr));

          monero_wallet[MK_URI].saddr = strndup(retaddr, MAX_ADDR_SIZE);

          if (amount == NULL) {
             fprintf(stdout, "%s\n", retaddr);
          } else {

            monero_wallet[MK_URI].amount = strndup(amount, MAX_ADDR_SIZE);

            if (0 > (ret = rpc_call(&monero_wallet[MK_URI]))) {
                fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[MK_URI].host,
                                                                      monero_wallet[MK_URI].port);
                exit(EXIT_FAILURE);
            }
            cJSON *result = cJSON_GetObjectItem(monero_wallet[MK_URI].reply, "result");
            cJSON *uri = cJSON_GetObjectItem(result, "uri");

            fprintf(stdout, "%s\n", delQuotes(cJSON_Print(uri)));
        }
    }

    /*
     * mnp-payment --newaddr
     * returns new created subaddress
     * mnp-payment --amount 10 --newaddr
     * returns uri with new subaddress, amount
     */
    if (new == 1) {
        if (0 > (ret = rpc_call(&monero_wallet[NEW_SUBADDR]))) {
            fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[NEW_SUBADDR].host,
                                                                  monero_wallet[NEW_SUBADDR].port);
            exit(EXIT_FAILURE);
        }
        cJSON *result = cJSON_GetObjectItem(monero_wallet[NEW_SUBADDR].reply, "result");
        cJSON *address = cJSON_GetObjectItem(result, "address");
        char *retaddr = delQuotes(cJSON_Print(address));
        monero_wallet[MK_URI].saddr = strndup(retaddr, MAX_ADDR_SIZE);

        if (amount == NULL) {
           fprintf(stdout, "%s\n", retaddr);
        } else {

            monero_wallet[MK_URI].amount = strndup(amount, MAX_ADDR_SIZE);

            if (0 > (ret = rpc_call(&monero_wallet[MK_URI]))) {
                fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[MK_URI].host,
                                                                      monero_wallet[MK_URI].port);
                exit(EXIT_FAILURE);
            }
            cJSON *result = cJSON_GetObjectItem(monero_wallet[MK_URI].reply, "result");
            cJSON *uri = cJSON_GetObjectItem(result, "uri");

            fprintf(stdout, "%s\n", delQuotes(cJSON_Print(uri)));
        }
    }

    /*
     * openssl rand --hex 8 | mnp-payment
     * returns integrated address.
     * mnp-payment --amount 10 1234567890abcdef
     * returns uri with new integrated adddress + paymentid + amount
     */
    if (paymentId != NULL) {
        if (strlen(paymentId) != 16) {
            fprintf(stderr, "Invalid payment Id. (16 characters hex)\n");
            exit(EXIT_FAILURE);
        }

        monero_wallet[MK_IADDR].payid = strndup(paymentId, MAX_PAYID_SIZE);
        if (0 > (ret = rpc_call(&monero_wallet[MK_IADDR]))) {
            fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[MK_IADDR].host,
                                                                  monero_wallet[MK_IADDR].port);
            exit(EXIT_FAILURE);
        }

        cJSON *result = cJSON_GetObjectItem(monero_wallet[MK_IADDR].reply, "result");
        cJSON *integrated_address = cJSON_GetObjectItem(result, "integrated_address");

        monero_wallet[MK_URI].saddr = strndup(delQuotes(cJSON_Print(integrated_address)), MAX_IADDR_SIZE);

        if (amount == NULL) {
           fprintf(stdout, "%s\n", delQuotes(cJSON_Print(integrated_address)));
        } else {

            monero_wallet[MK_URI].amount = strndup(amount, MAX_ADDR_SIZE);

            if (0 > (ret = rpc_call(&monero_wallet[MK_URI]))) {
                fprintf(stderr, "could not connect to host: %s:%s\n", monero_wallet[MK_URI].host,
                                                                      monero_wallet[MK_URI].port);
                exit(EXIT_FAILURE);
            }
            cJSON *result = cJSON_GetObjectItem(monero_wallet[MK_URI].reply, "result");
            cJSON *uri = cJSON_GetObjectItem(result, "uri");

            fprintf(stdout, "%s\n", delQuotes(cJSON_Print(uri)));
        }
    }

    free(monero_wallet);
    free(account);
    return 0;
}


/*
 * read paymentId from stdin
 */
static char *readStdin(void)
{
    char *buffer;
    int ret;

    buffer = (char *)malloc(MAX_PAYID_SIZE+1);
    if(buffer == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    ret = fread(buffer, 1, MAX_PAYID_SIZE, stdin);
    if(ret == 0) {
        fprintf(stderr, "No input data.\n");
        exit(EXIT_FAILURE);
    }

    buffer[ret] = '\0';

    return buffer;
}



/*
 * Parse INI file handler
 */
static int handler(void *user, const char *section, const char *name,
                   const char *value)
{
    struct Config *pconfig = (struct Config*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("rpc", "user")) {
        pconfig->rpc_user = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "password")) {
        pconfig->rpc_password = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "host")) {
        pconfig->rpc_host = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "port")) {
        pconfig->rpc_port = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        pconfig->mnp_account = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnppayment", "subaddress")) {
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}



/*
 * Print user help.
 */
static void usage(int status)
{
    int ok = status ? 0 : 1;
    if (ok)
    fprintf(stdout,
    "Usage: mnp-payment [OPTION] [PAYMENT_ID]\n\n"
    "       [PAYMENT_ID]\n"
    "               [PAYMENT_ID is a 16 hex unique char to\n"
    "               identify the payment. Returns an\n"
    "               integrated address.\n\n"
    "  -a  --account [ACCOUNT]\n"
    "               Monero account number. 0 = default.\n\n"
    "      --rpc_user [RPC_USER]\n"
    "               rpc user for monero-wallet-rpc.\n"
    "               Needed for authetification @ rpc wallet.\n\n"
    "      --rpc_password [RPC_PASSWORD]\n"
    "               rpc password for monero-wallet-rpc.\n"
    "               Needed for authetification @ rpc wallet.\n\n"
    "      --rpc_host [RPC_HOST]\n"
    "               rpc host ip address or domain.\n\n"
    "      --rpc_port [RPC_PORT]\n"
    "               rpc port to cennect to.\n\n"
    "  -l, --list\n"
    "               list all subaddresses + address_indices.\n\n"
    "  -s  --subaddr [INDEX]\n"
    "               returns subaddress on INDEX.\n\n"
    "  -n  --newaddr\n"
    "               returns a new created subaddress.\n\n"
    "  -x  --amount [AMOUNT]\n"
    "               The amount is specified in pcionero.\n"
    "               returns an URI string.\n\n"
    "  -v, --version\n"
    "               Display the version number of mnp.\n\n"
    "  -h, --help   Display this help message.\n"
    );
    else
    fprintf(stderr,
    "Use mnp --help for more information\n"
    "Monero Named Pipes.\n"
    );
}


/*
 * Print version of Monero Named Pipes.
 */
static void printmnp(void)
{
                printf("\033[0;32m"
                "       __        \n"
                "  w  c(..)o    ( \n"
                "   \\__(-)    __) \n"
                "       /\\   (    \n"
                "      /(_)___)   \n"
                "     w /|        \n"
                "      | \\        \n"
                "      m  m \033[0m Monero Named Pipes. mnp-payment Version: %s\n\n", VERSION);
}

