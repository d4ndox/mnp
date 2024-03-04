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
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "delquotes.h"
#include "./inih/ini.h"
#include "./cjson/cJSON.h"
#include "wallet.h"
#include "rpc_call.h"
#include "version.h"
#include "bc_height.h"
#include "balance.h"
#include "globaldefs.h"
#include <inttypes.h>
#include <unistd.h>

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
	{"init"         , no_argument      , NULL, 'n'},
	{"cleanup"      , no_argument      , NULL, 'c'},
        {"version"      , no_argument      , NULL, 'v'},
	{"verbose"      , no_argument      , &verbose, 1},
	{NULL, 0, NULL, 0}
};

static char *optstring = "hu:r:i:p:a:w:ncvl";
static void usage(int status);
static int handler(void *user, const char *section, const char *name, const char *value);
static void initshutdown(int);
static int remove_directory(const char *path);
static void printmnp(void);
static char *readStdin(void);

int main(int argc, char **argv)
{
    /* keeps the old status of blockchain hight
     * needs to be compared with every rpc call
     * write to fifo pipe if value changed
     */
    char *status_bc_height = NULL;
    char *status_balance = NULL;

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
    char *transferdir = NULL;
    char *paymentdir = NULL;

    int init = 0;
    int cleanup = 0;

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
            case 'n':
                init = 1;
                break;
            case 'c':
                cleanup = 1;
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


    if (init == 0 && cleanup == 0) {
        if (optind < argc) {
            txid = (char *)argv[optind];
        }
        if (txid == NULL) {
            txid = readStdin();
        }
    }

    fprintf(stderr, "txid = %s\n", txid);
    const char *perm = strndup(config.cfg_mode, MAX_DATA_SIZE);
    mode_t mode = (((perm[0] == 'r') * 4 | (perm[1] == 'w') * 2 | (perm[2] == 'x')) << 6) |
                  (((perm[3] == 'r') * 4 | (perm[4] == 'w') * 2 | (perm[5] == 'x')) << 3) |
                  (((perm[6] == 'r') * 4 | (perm[7] == 'w') * 2 | (perm[8] == 'x')));

    if (DEBUG) fprintf(stderr, "mode_t = %03o and mode = %s\n", mode, config.cfg_mode);
    
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
        /* tx related */
        monero_wallet[i].txid = NULL;
        monero_wallet[i].payid = NULL;
        monero_wallet[i].saddr = NULL;
        monero_wallet[i].iaddr = NULL;
        monero_wallet[i].amount = NULL;
        monero_wallet[i].conf = NULL;
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

    //monero_rpc_wallet.
    /* prepare url and port to connect to the wallet */
    char *urlport = NULL;

    if (rpc_host != NULL && rpc_port != NULL) {
        asprintf(&urlport,"http://%s:%s/json_rpc", rpc_host, rpc_port);
    } else {
        fprintf(stderr, "rpc_host and/or rpc_port is missing\n");
        exit(EXIT_FAILURE);
    }

    /* prepare rpc_user and rpc_password to connect to the wallet */
    char *userpwd = NULL;

    if (rpc_user != NULL && rpc_password != NULL) {
        asprintf(&userpwd,"%s:%s", rpc_user, rpc_password);
    } else {
        fprintf(stderr, "rpc_user and/or rpc_password is missing\n");
        exit(EXIT_FAILURE);
    }

    /* if no account is set - use the default account 0 */
    if (account == NULL) asprintf(&account, "0");

    if (verbose) {
        fprintf(stdout, "Starting ... \n");
        printmnp();
    }

    /* create workdir directory. TEST if directory does exist */
    struct stat sb;

    if (stat(workdir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        /*NOP*/
        fprintf(stderr, "work\n");
    } else if (mkdir(workdir, mode)) {
        fprintf(stderr, "Could not create workdir %s.\n", workdir);
        exit(EXIT_FAILURE);
    }

    /* create transferdir directory. TEST if directory does exist */
    struct stat transfer;

    asprintf(&transferdir, "%s/%s", workdir, TRANSFER_DIR);
    if (stat(transferdir, &transfer) == 0 && S_ISDIR(transfer.st_mode)) {
        /* NOP */
        fprintf(stderr, "tr\n");
    } else if (mkdir(transferdir, mode)) {
        fprintf(stderr, "Could not create transferdir %s.\n", transferdir);
        exit(EXIT_FAILURE);
    }

    /* create paymentdir directory. TEST if directory does exist */
    struct stat payment;

    asprintf(&paymentdir, "%s/%s", workdir, PAYMENT_DIR);
    if (stat(paymentdir, &payment) == 0 && S_ISDIR(payment.st_mode)) {
        /* NOP */
        fprintf(stderr, "pay\n");
    } else if (mkdir(paymentdir, mode) && errno != EEXIST) {
        fprintf(stderr, "Could not create paymentdir %s.\n", paymentdir);
        exit(EXIT_FAILURE);
    } if (init) exit(0);

    if (cleanup) {
        remove_directory(transferdir);
        remove_directory(paymentdir);
        remove_directory(workdir);
        exit(0);
    }

    /* 
     * declare and initalise 
     * named pipe (mkfifo)
     * BC_HEIGHT
     * BALANCE
     * SETUP_TRANSFER
     * SETUP_PAYMENT
     * cJSON object »bc_height«
     */
    char *bc_height_fifo = NULL;
    char *balance_fifo = NULL;
    char *setup_transfer_fifo = NULL;
    char *setup_payment_fifo = NULL;

    ret = 0;
    asprintf(&bc_height_fifo, "%s/%s", workdir, BC_HEIGHT_FILE);
    asprintf(&balance_fifo, "%s/%s", workdir, BALANCE_FILE);
    fprintf(stderr, "setup_transfer_fifo = %s\n", setup_transfer_fifo);

    if (mkfifo(bc_height_fifo, mode)) {
            fprintf(stderr, "Could not create named pipe bc_height %s\n", bc_height_fifo);
            exit(EXIT_FAILURE);
    }
    if (mkfifo(balance_fifo, mode)) {
            fprintf(stderr, "Could not create named pipe balance %s\n", balance_fifo);
            exit(EXIT_FAILURE);
    }
    if (mkfifo(setup_transfer_fifo, mode)) {
            fprintf(stderr, "Could not create named pipe transfer %s\n", setup_transfer_fifo);
            exit(EXIT_FAILURE);
    }
    if (mkfifo(setup_payment_fifo, mode)) {
            fprintf(stderr, "Could not create named pipe payment %s\n", setup_payment_fifo);
            exit(EXIT_FAILURE);
    }


    /* 
     * Start main loop
     */
    fprintf(stdout, "Running\n");
    while (running) {

    for (int i = 0; i < (END_RPC_SIZE-6); i++) {
    if (0 > (ret = rpc_call(&monero_wallet[i]))) {
        fprintf(stderr, "could not connect to host: %s\n", urlport);
        exit(EXIT_FAILURE);
    }
   
    switch (i) {
        case GET_HEIGHT:
            status_bc_height = blockchainheight(&(monero_wallet[i]).reply, bc_height_fifo, status_bc_height);
            if (status_bc_height == NULL) {
                fprintf(stderr, "could not parse JSON object height\n");
                remove_directory(workdir);
                exit(EXIT_FAILURE);
            }
            break;
        case GET_BALANCE:
            status_balance = balance(&(monero_wallet[i]).reply, balance_fifo, status_balance);
            if (status_balance == NULL) {
                fprintf(stderr, "could not parse JSON object balance\n");
                remove_directory(workdir);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "See main loop (END_RPC_SIZE-x) adjust x to the correct size\n");
            break;
    }
    } /* end for loop */
    ret = usleep(SLEEPTIME); 
    } /* end while loop */

    /* delete json + workdir and exit */
    unlink(bc_height_fifo);
    unlink(balance_fifo);
    unlink(setup_transfer_fifo);
    unlink(setup_payment_fifo);
    free(monero_wallet);
    free(status_bc_height);
    free(status_balance);
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
    "  -w, --workdir]\n"
    "               open Monero Named Pipes here.\n\n"
    "      --init\n"
    "               create workdir for usage.\n\n"
    "      --cleanup\n"
    "               delete workdir.\n\n"
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
 * read paymentId from stdin 
 */
static char *readStdin(void)
{
    char *buffer;
    int ret;

    buffer = (char *)malloc(MAX_TXID_SIZE+1);
    if(buffer == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    ret = fread(buffer, 1, MAX_TXID_SIZE, stdin);
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
    } else if (MATCH("mnp", "verbose")) {
        pconfig->mnp_verbose = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        pconfig->mnp_account = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "workdir")) {
        pconfig->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "mode")) {
        pconfig->cfg_mode = strndup(value, MAX_DATA_SIZE);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

/*
 * Stop the mainloop and destroy all fifo's
 */
static void initshutdown(int sig)
{
    running = 0;
}

/*
 * remove the workdir
 */
static int remove_directory(const char *path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d) {
        struct dirent *p;
        r = 0;

        while (!r && (p=readdir(d)))
        {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2;
            buf = malloc(len);

            if (buf) {
                struct stat statbuf;

                snprintf(buf, len, "%s/%s", path, p->d_name);

                if (!stat(buf, &statbuf)) {
                    if (S_ISDIR(statbuf.st_mode)) {
                        r2 = remove_directory(buf);
                    } else {
                        r2 = unlink(buf);
                    }
                }

                free(buf);
            }

            r = r2;
        }

        closedir(d);
    }

    if (!r) {
        r = rmdir(path);
    }

    return r;
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
                "      m  m \033[0m Monero Named Pipes. Version: %s\n\n", VERSION);
}

