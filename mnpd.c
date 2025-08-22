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

/* std. c libraries */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/* system headers */
#include <sys/stat.h>
#include <syslog.h>

/* third party libraries */
#include "./cjson/cJSON.h"
#include <curl/curl.h>
#include "./inih/ini.h"

/* local headers */
#include "globaldefs.h"
#include "rpc_call.h"
#include "wallet.h"

/* verbose is extern @ globaldefs.h. Be noisy.*/
int verbose = 0;

static volatile sig_atomic_t running = 1;

static const struct option options[] = {
    {"help"         , no_argument      , NULL, 'h'},
    {"rpc_user"     , required_argument, NULL, 'u'},
    {"rpc_password" , required_argument, NULL, 'r'},
    {"rpc_host"     , required_argument, NULL, 'i'},
    {"rpc_port"     , required_argument, NULL, 'p'},
    {"account"      , required_argument, NULL, 'a'},
    {"workdir"      , required_argument, NULL, 'w'},
    {"version"      , no_argument      , NULL, 'v'},
    {"verbose"      , no_argument      , &verbose, 1},
    {NULL, 0, NULL, 0}
};

static char *optstring = "hu:r:i:p:a:w:vl";
static void usage(int status);
static int handler(void *user, const char *section, const char *name, const char *value);
static void initshutdown(int);
static void printmnp(void);
static char *balance(const struct rpc_wallet *monero_wallet);
static char *bcheight(const struct rpc_wallet *monero_wallet);


/**
 * Main function to execute the Monero Named Pipes Daemon program.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on successful execution, EXIT_FAILURE on error.
 */
int main(int argc, char **argv)
{
    /* open syslog /var/log/messages and /var/log/syslog */
    openlog("mnpd:", LOG_PID, LOG_USER);

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
    char *txid = NULL;
    char *workdir = NULL;
    int ret = 0;

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
            case 'w':
                workdir = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'v':
                printmnp();
                exit(EXIT_SUCCESS);
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
    } if (workdir == NULL) {
        workdir = strndup(config.cfg_workdir, MAX_DATA_SIZE);
    } if (verbose == 0) {
        verbose = atoi(config.mnp_verbose);
    }

    const char *perm = strndup(config.cfg_mode, MAX_DATA_SIZE);
    mode_t mode = (((perm[0] == 'r') * 4 | (perm[1] == 'w') * 2 | (perm[2] == 'x')) << 6) |
                  (((perm[3] == 'r') * 4 | (perm[4] == 'w') * 2 | (perm[5] == 'x')) << 3) |
                  (((perm[6] == 'r') * 4 | (perm[7] == 'w') * 2 | (perm[8] == 'x')));

    perm = strndup(config.cfg_pipe, MAX_DATA_SIZE);
    mode_t pmode = (((perm[0] == 'r') * 4 | (perm[1] == 'w') * 2 | (perm[2] == 'x')) << 6) |
                   (((perm[3] == 'r') * 4 | (perm[4] == 'w') * 2 | (perm[5] == 'x')) << 3) |
                   (((perm[6] == 'r') * 4 | (perm[7] == 'w') * 2 | (perm[8] == 'x')));

    if (DEBUG) fprintf(stderr, "mode_t = %03o and mode = %s\n", mode, config.cfg_mode);
    if (DEBUG) fprintf(stderr, "pmode_t = %03o and mode = %s\n", pmode, config.cfg_pipe);

    struct rpc_wallet *monero_wallet = (struct rpc_wallet*)malloc(END_RPC_SIZE * sizeof(struct rpc_wallet));
    if (DEBUG) printf("enum size = %d\n", END_RPC_SIZE);

    /* initialise monero_wallet with NULL */
    for (int i = 0; i < END_RPC_SIZE; i++) {
        monero_wallet[i].monero_rpc_method = i;
        monero_wallet[i].params = NULL;
        monero_wallet[i].account = NULL;
        monero_wallet[i].host = NULL;
        monero_wallet[i].port = NULL;
        monero_wallet[i].user = NULL;
        monero_wallet[i].pwd = NULL;
        /* mnpd relatted */
        monero_wallet[i].balance = NULL;
        monero_wallet[i].height = NULL;
        monero_wallet[i].file = NULL;
        /* tx related */
        monero_wallet[i].txid = NULL;
        monero_wallet[i].payid = NULL;
        monero_wallet[i].saddr = NULL;
        monero_wallet[i].iaddr = NULL;
        monero_wallet[i].amount = NULL;
        monero_wallet[i].conf = NULL;
        monero_wallet[i].locked = NULL;
        monero_wallet[i].fifo = NULL;
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
            syslog(LOG_USER | LOG_ERR, "rpc_host is missing");
            fprintf(stderr, "mnpd: rpc_host is missing\n");
            closelog();
            exit(EXIT_FAILURE);
        }
        if (rpc_port != NULL) {
            monero_wallet[i].port = strndup(rpc_port, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_port is missing");
            fprintf(stderr, "mnpd: rpc_port is missing\n");
            closelog();
            exit(EXIT_FAILURE);
        }
        if (rpc_user != NULL) {
            monero_wallet[i].user = strndup(rpc_user, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_user is missing");
            fprintf(stderr, "mnpd: rpc_user is missing\n");
            closelog();
            exit(EXIT_FAILURE);
        }
        if (rpc_password != NULL) {
            monero_wallet[i].pwd = strndup(rpc_password, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_password is missing");
            fprintf(stderr, "mnpd: rpc_password is missing\n");
            closelog();
            exit(EXIT_FAILURE);
        }
    }


    /* if no account is set - use the default account 0 */
    if (account == NULL) asprintf(&account, "0");

    if (verbose) {
        fprintf(stdout, "Starting ... \n");
        printmnp();
    }

    /* TEST if work directory does exist */
    struct stat sb;

    if (stat(workdir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        /*NOP*/
    } else {
        fprintf(stderr, "Could not find workdir %s. Run mnp --init\n", workdir);
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "Working directory: %s\n", workdir);

    /*
     * Start main loop
     */
    fprintf(stdout, "Running\n");
    while (running) {

        for (int i = 0; i < 2; i++) {
            int ret = 0;
            if (0 > (ret = rpc_call(&monero_wallet[i]))) {
                syslog(LOG_USER | LOG_ERR, "could not connect to host: %s:%s", monero_wallet[i].host,
                                                                               monero_wallet[i].port);
                fprintf(stderr, "mnpd: could not connect to host: %s:%s\n", monero_wallet[i].host,
                                                                           monero_wallet[i].port);
                closelog();
                exit(EXIT_FAILURE);
            }
            switch (i) {
                case GET_HEIGHT:
                   if (monero_wallet[i].height == NULL) {
                       asprintf(&monero_wallet[i].height, "%s", "-1");
                   }

                   if (strcmp(bcheight(&monero_wallet[i]), monero_wallet[i].height) == 0) {
                       break;
                   }

                   asprintf(&monero_wallet[i].file, "%s/%s", workdir, BC_HEIGHT_FILE);
                   asprintf(&monero_wallet[i].height, "%s", bcheight(&monero_wallet[i]));

                   FILE *fdh = fopen(monero_wallet[i].file, "w");
                       if (fdh == NULL) {
                       syslog(LOG_USER | LOG_ERR, "error: %s", strerror(errno));
                       fprintf(stderr, "mnpd1: error: %s", strerror(errno));
                       closelog();
                       exit(EXIT_FAILURE);
                   }

                    if (chmod(monero_wallet[i].file, pmode) == -1) {
                        syslog(LOG_USER | LOG_ERR, "error: %s", strerror(errno));
                        fprintf(stderr, "mnpd2: error: %s", strerror(errno));
                        closelog();
                        fclose(fdh);
                        exit(EXIT_FAILURE);
                   }

                   ssize_t retheight = fprintf(fdh, "%s\n", monero_wallet[i].height);
                   if (retheight < 0) {
                        syslog(LOG_USER | LOG_ERR, "error: write %s", strerror(errno));
                        fprintf(stderr, "mnpd3: error: %s", strerror(errno));
                        closelog();
                        exit(EXIT_FAILURE);
                   }
                   fclose(fdh);
                   break;
                case GET_BALANCE:

                   if (monero_wallet[i].balance == NULL) {
                       asprintf(&monero_wallet[i].balance, "%s", "-1");
                   }

                   if (strcmp(balance(&monero_wallet[i]), monero_wallet[i].balance) == 0) {
                       break;
                   }

                   asprintf(&monero_wallet[i].file, "%s/%s", workdir, BALANCE_FILE);
                   asprintf(&monero_wallet[i].balance, "%s", balance(&monero_wallet[i]));

                   FILE *fdb = fopen(monero_wallet[i].file, "w");
                   if (fdb == NULL) {
                        syslog(LOG_USER | LOG_ERR, "error: %s", strerror(errno));
                        fprintf(stderr, "mnpd1: error: %s", strerror(errno));
                        closelog();
                        exit(EXIT_FAILURE);
                   }

                   if (chmod(monero_wallet[i].file, pmode) == -1) {
                        syslog(LOG_USER | LOG_ERR, "error: %s", strerror(errno));
                        fprintf(stderr, "mnpd2: error: %s", strerror(errno));
                        closelog();
                        fclose(fdh);
                        exit(EXIT_FAILURE);
                   }

                   ssize_t retbalance = fprintf(fdb, "%s\n",  monero_wallet[i].balance);
                   if (retbalance < 0) {
                        syslog(LOG_USER | LOG_ERR, "error: write %s", strerror(errno));
                        fprintf(stderr, "mnpd3: error: %s", strerror(errno));
                        closelog();
                        exit(EXIT_FAILURE);
                    }

                    fclose(fdb);
                    break;
                default:
                    fprintf(stderr, "See main loop (END_RPC_SIZE-x) adjust x to the correct size\n");
                    break;
            }
        } /* end for loop */
        ret = sleep(SLEEPTIME);
    } /* end while loop */

    free(monero_wallet);
    exit(EXIT_SUCCESS);
}


/**
 * Extracts the total balance from the Monero wallet RPC response.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A dynamically allocated string containing the amount, or NULL if the extraction fails.
 */
static char *balance(const struct rpc_wallet *monero_wallet)
{
    assert (monero_wallet != NULL);

    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *balance = cJSON_GetObjectItem(result, "balance");

    return cJSON_Print(balance);
}


/**
 * Extracts the blockchain height from the Monero wallet RPC response.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A dynamically allocated string containing the amount, or NULL if the extraction fails.
 */
static char *bcheight(const struct rpc_wallet *monero_wallet)
{
    assert (monero_wallet != NULL);

    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *height = cJSON_GetObjectItem(result, "height");

    return cJSON_Print(height);
}


/**
 * Prints user help information.
 *
 * @param status The status code to determine the output stream (0 for success, non-zero for error).
 */
static void usage(int status)
{
    int ok = status ? 0 : 1;
    if (ok)
    fprintf(stdout,
    "Usage: mnpd [OPTION]\n\n"
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
    "  -w, --workdir  [WORKDIR]\n"
    "               place to create the work directory.\n\n"
    "  -v, --version\n"
    "               Display the version number of mnp.\n\n"
    "      --verbose\n"
    "               Display verbose information to stderr.\n\n"
    "  -h, --help   Display this help message.\n"
    );
    else
    fprintf(stderr,
    "Use mnpd --help for more information\n"
    "Monero Named Pipes Daemon.\n"
    );
}


/**
 * Parses the INI file and handles the configuration settings.
 *
 * @param user A pointer to the user data structure (Config).
 * @param section The section name in the INI file.
 * @param name The name of the setting in the INI file.
 * @param value The value of the setting in the INI file.
 * @return 1 on success, 0 on failure.
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
    } else if (MATCH("mnp", "verbose")) {
        pconfig->mnp_verbose = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        pconfig->mnp_account = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "workdir")) {
        pconfig->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "mode")) {
        pconfig->cfg_mode = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "pipe")) {
        pconfig->cfg_pipe = strndup(value, MAX_DATA_SIZE);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}


/**
 * Handles the shutdown signal and stops the main loop.
 *
 * @param sig The signal number received.
 */
static void initshutdown(int sig)
{
    running = 0;
}


/**
 * Prints the version information of Monero Named Pipes.
 * ASCII art by ejm. Probs to ejm for this awesome art!
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
                "      m  m \033[0m Monero Named Pipes Daemon. Version: %s\n\n", VERSION);
}
