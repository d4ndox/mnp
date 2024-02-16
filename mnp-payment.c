/* Monero Named Pipes (mnp-payment) is a Unix Monero payment processor.
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
#include "./inih/ini.h"
#include "./cjson/cJSON.h"
#include "rpc_call.h"
#include "version.h"
#include "globaldefs.h"

static const struct option options[] = {
	{"help"         , no_argument      , NULL, 'h'},
        {"rpc_user"     , required_argument, NULL, 'u'},
        {"rpc_password" , required_argument, NULL, 'r'},
        {"rpc_host"     , required_argument, NULL, 'i'},
        {"rpc_port"     , required_argument, NULL, 'p'},
        {"account"      , required_argument, NULL, 'a'},
        {"subaddr"      , required_argument, NULL, 's'},
        {"version"      , no_argument      , NULL, 'v'},
        {"list"         , no_argument      , NULL, 'l'},
	{NULL, 0, NULL, 0}
};

static int handler(void *user, const char *section,
                   const char *name, const char *value);
static char *optstring = "hu:r:i:p:a:s:vl";
static void usage(int status);
static void printmnp(void);

int main(int argc, char **argv)
{
    /* variables are set by getopt and/or config parser handler()*/
    int opt, lindex = -1;
    char *rpc_user = NULL;
    char *rpc_password = NULL;
    char *rpc_host = NULL;
    char *rpc_port = NULL;
    char *account = NULL;
    int subaddr = -1;

    int list = 0;
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

    struct rpc_wallet *monero_wallet = (struct rpc_wallet*)malloc(END_RPC_SIZE * sizeof(struct rpc_wallet));
    if (DEBUG) printf("enum size = %d\n", END_RPC_SIZE);
    if (DEBUG) printf("GET_VERSION = %d\n", GET_VERSION);
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

    if (DEBUG) {
    for (int i = 0; i < END_RPC_SIZE; i++) {
        printf("monero_rpc_method[%d] = %d\n", i, monero_wallet[i].monero_rpc_method);
        printf("account[%d] = %s\n", i, monero_wallet[i].account);
        printf("port[%d] = %s\n", i, monero_wallet[i].port);
    }}

    /* if no account is set - use the default account 0 */
    if (account == NULL) asprintf(&account, "0");

    if (list == 1) {
          asprintf(&(monero_wallet[GET_LIST]).params, "{\"account_index\":%s}", account);

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
              fprintf(stderr, "%s %s\n", cJSON_Print(idx),
                                                    cJSON_Print(adr));
          }
    }

    if (subaddr >= 0) {

    }
    return 0;
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
    "Usage: mnp-payment [OPTION]\n\n"
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
