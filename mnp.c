/*
 * Monero Named Pipes (mnp) is a Unix Monero wallet client.
 * Copyright (C) 2019 https://github.com/nonie-sys/mnp
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdio.h>
#include <curl/curl.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "./inih/ini.h"
#include "./cjson/cJSON.h"
#include "wallet.h"
#include "rpc_call.h"
#include "out_version.h"
#include "globaldefs.h"
#include <inttypes.h>

/* verbose is extern @ globaldefs.h. Be noisy.*/
int verbose = 0;
/* daemon is extern @ globaldefs.h. Run in background */
int daemonflag = 0;

static volatile sig_atomic_t running = 1;

static const struct option options[] = {
	{"help"         , no_argument      , NULL, 'h'},
        {"rpc_user"     , required_argument, NULL, 'u'},
        {"rpc_password" , required_argument, NULL, 'r'},
        {"rpc_host"     , required_argument, NULL, 'i'},
        {"rpc_port"     , required_argument, NULL, 'p'},
        {"account"      , required_argument, NULL, 'a'},
        {"version"      , no_argument      , NULL, 'v'},
	{"verbose"      , no_argument      , &verbose, 1},
	{"daemon"       , no_argument      , &daemonflag, 'd'},
	{NULL, 0, NULL, 0}
};

static char *optstring = "hu:r:i:p:a:vd";
static void usage(int status);
static int handler(void *user, const char *section,
                   const char *name, const char *value);
static int daemonize(void);
static void initshutdown(int);
static void printmnp(void);

int main(int argc, char **argv)
{
    /* signal handler for shutdown */
    signal(SIGHUP, initshutdown);
    signal(SIGINT, initshutdown);
    signal(SIGQUIT, initshutdown);
    signal(SIGTERM, initshutdown);
    signal(SIGPIPE, SIG_IGN);

    /* variables are set by getopt and/or config parser handler()*/
    int opt, lindex = -1;
    char *rpc_user = NULL;
    char *rpc_password = NULL;
    char *rpc_host = NULL;
    char *rpc_port = NULL;
    char *account = NULL;

    /* prepare for reading the config ini file */
    const char *homedir;

    if ((homedir = getenv("home")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    char *ini = NULL;
    char *home = strndup(homedir, MAX_DATA_SIZE);
    asprintf(&ini, "%s/%s", home, CONFIG_FILE);
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
                //asprintf(&rpc_user, "%s", optarg);
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
            case 'v':
                printmnp();
                exit(EXIT_SUCCESS);
                break;
            case 'd':
                daemonflag = 1;
                break;
            case 0:
	        break;
            default:
	        break;
        }
    }

    /* if no command line option is set - use the config ini file */
    if (account == NULL) {
        asprintf(&account, "%s", config.mnp_account);
    } if (rpc_user == NULL) {
        asprintf(&rpc_user, "%s", config.rpc_user);
    } if (rpc_password == NULL) {
        asprintf(&rpc_password, "%s", config.rpc_password);
    } if (rpc_host == NULL) {
        asprintf(&rpc_host, "%s", config.rpc_host);
    } if (rpc_port == NULL) {
        asprintf(&rpc_port, "%s", config.rpc_port);
    } if (verbose == 0) {
        verbose = atoi(config.mnp_verbose);
    } if (daemonflag == 0) {
        daemonflag = atoi(config.mnp_daemon);
    }

    /* Prepare url and port to connect to the wallet */
    char *urlport = NULL;

    if (rpc_host != NULL && rpc_port != NULL)
    {
        asprintf(&urlport,"http://%s:%s/json_rpc", rpc_host, rpc_port);
    } else {
        fprintf(stderr, "rpc_host and/or rpc_port is missing\n");
        exit(EXIT_FAILURE);
    }
    
    /* Prepare rpc_user and rpc_password to connect to the wallet */
    char *userpwd = NULL;

    if (rpc_user != NULL && rpc_password != NULL)
    {
        asprintf(&userpwd,"%s:%s", rpc_user, rpc_password);
    } else {
        fprintf(stderr, "rpc_user and/or rpc_password is missing\n");
        exit(EXIT_FAILURE);
    }

    /* if no account is set - use the default account 0 */
    if (account == NULL)
    {
        asprintf(&account, "0");
    }

    if (verbose)
    {
        fprintf(stdout, "Starting ... \n");
        printmnp();
    }

    fprintf(stdout, "Connecting: %s\n", urlport);

     /* GET_VERSION rpc call for version of rpc protocol*/
    int ret = 0;
    cJSON *version = NULL;
    ret = 0;
    if (0 > (ret = rpc_call(GET_VERSION, NOPARAMS, urlport, userpwd, &version))) {
        fprintf(stderr, "could not connect to host: %s\n", urlport);
        exit(EXIT_FAILURE);
    }
    if (0 > (out_version(&version))) {
        fprintf(stderr, "could not parse JSON object version\n");
        exit(EXIT_FAILURE);
    }
    char *rpc_version = cJSON_Print(version);

    /* Start */
    fprintf(stdout, "Running\n");
    if (daemonflag == 1) {
        int retd = daemonize();
        if (verbose && (retd == 0)) fprintf(stdout, "daemon started.\n");
        sleep(5);
        fprintf(stdout, "daemon end\n");
    }

    while (running) {   }
    if (DEBUG) fprintf(stdout, "%s\n", rpc_version);

    cJSON *bc_height = NULL;
    ret = 0;
    if (0 > (ret = rpc_call(GET_HEIGHT, NOPARAMS, urlport, userpwd, &bc_height))) {
        fprintf(stderr, "could not connect to host: %s\n", urlport);
        exit(EXIT_FAILURE);
    }
    const cJSON *bc_result = NULL;
    bc_result = cJSON_GetObjectItem(bc_height, "result");

    char *res = cJSON_Print(bc_result);
    fprintf(stdout, "result = %s\n", res);

    cJSON_Delete(version);
    cJSON_Delete(bc_height);
}

/*
 * Print user help.
 */
static void usage(int status)
{
    int ok = status ? 0 : 1;
    if (ok)
    fprintf(stdout,
    "Usage: mnp [OPTION]\n\n"
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
    "  -d, --daemon]\n"
    "               run mnp as daemon.\n\n"
    "  -v, --version\n"
    "               Display the version number of mnp.\n\n"
    "      --verbose\n"
    "               Display verbose information to stderr.\n\n"
    "  -h, --help   Display this help message.\n"
    );
    else
    fprintf(stderr,
    "Use mnp --help for more information\n"
    "Monero Named Pipes.\n"
    );
}

/*
 * Daemonize
 */
int daemonize(void)
{
    pid_t   pid;

    if ((pid = fork()) < 0)
        return(-1);
    else if (pid !=0)
        exit(0);

    setsid();
    return(0);
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
    } else if (MATCH("mnp", "daemon")) {
        pconfig->mnp_daemon = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "verbose")) {
        pconfig->mnp_verbose = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        pconfig->mnp_account = strndup(value, MAX_DATA_SIZE);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

/* Stop the mainloop and destroy all fifo's */
static void initshutdown(int sig)
{
    running = 0;
}

/*
 * Print version of Monero Named Pipes.
 */
static void printmnp(void) {
                printf("\033[0;32m"
                "       __        \n"
                "  w  c(..)o    ( \n"
                "   \\__(-)    __) \n"
                "       /\\   (    \n"
                "      /(_)___)   \n"
                "     w /|        \n"
                "      | \\        \n"
                "      m  m \033[0m Monero Named Pipes. Version: %s\n\n", VERSION);
}

