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
#include <fcntl.h>
#include <ftw.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/* system headers */
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>

/* third party libraries */
#include "./cjson/cJSON.h"
#include <curl/curl.h>
#include "./inih/ini.h"

/* local headers */
#include "delquotes.h"
#include "globaldefs.h"
#include "rpc_call.h"
#include "validate.h"
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
    {"notify-at"    , required_argument, NULL, 'o'},
    {"confirmation" , required_argument, NULL, 'n'},
    {"message"      , required_argument, NULL, 'm'},
    {"signature"    , required_argument, NULL, 'g'},
    {"address"      , required_argument, NULL, 'd'},
    {"spend-proof"  , no_argument      , NULL, 's'},
    {"tx-proof"     , no_argument      , NULL, 'x'},
    {"init"         , no_argument      , NULL, 't'},
    {"retry"        , no_argument      , NULL, 'R'},
    {"cleanup"      , no_argument      , NULL, 'c'},
    {"version"      , no_argument      , NULL, 'v'},
    {"verbose"      , no_argument      , &verbose, 1},
    {NULL, 0, NULL, 0}
};

static const char *optstring = ":hu:r:i:p:a:w:o:n:m:g:d:sxtcRv";
static void usage(int status);
static int handler(void *user, const char *section, const char *name, const char *value);
static void initshutdown(int);
static int remove_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
static int remove_directory(const char *path);
static void printmnp(void);
static char *readStdin(void);
static char *get_confirm(const cJSON *transfer);
static char *get_locked(const cJSON *transfer);
static char *proof(const struct rpc_wallet *monero_wallet);
static char *proof_confirm(const struct rpc_wallet *monero_wallet);
static char *proof_received(const struct rpc_wallet *monero_wallet);
static cJSON *get_transfers(struct rpc_wallet *monero_wallet);
static void write_to_pipe(const char *pipe, const char *content);
static int get_env_int(const char *name, int fallback);


/**
 * Main function to execute the Monero Named Pipes program.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on successful execution, EXIT_FAILURE on error.
 */
int main(int argc, char **argv)
{
    /* open syslog /var/log/messages and /var/log/syslog */
    openlog("mnp:", LOG_PID, LOG_USER);

    /* signal handler for shutdown */
    signal(SIGHUP, initshutdown);
    signal(SIGINT, initshutdown);
    signal(SIGQUIT, initshutdown);
    signal(SIGTERM, initshutdown);
    signal(SIGPIPE, SIG_IGN);

    /* variables are set by getopt and/or config parser handler()*/
    char *locked0 = "true";
    int opt, lindex = -1;
    char *rpc_user = NULL;
    char *rpc_password = NULL;
    char *rpc_host = NULL;
    char *rpc_port = NULL;
    char *account = NULL;

    int poll_interval = get_env_int("MNP_POLL_INTERVAL", POLL_INTERVAL);

    struct rpc_wallet *monero_wallet = (struct rpc_wallet*)malloc(END_RPC_SIZE * sizeof(struct rpc_wallet));
    if (!monero_wallet) { perror("malloc"); exit(EXIT_FAILURE); }

    char *txid = NULL;
    int txid_from_stdin = 0;
    char *workdir = NULL;
    char *txdir = NULL;
    char *txId = NULL;
    char *txid_pipe = NULL;
    char *double_spend_pipe = NULL;
    char *rpc_conn_alert_pipe = NULL;
    char *message = NULL;
    char *signature = NULL;
    char *adr = NULL;
    int sp_proof = 0;
    int tx_proof = 0;
    int init = 0;
    int cleanup = 0;
    int confirmation = 0;
    int notify = CONFIRMED;
    int retry = 0;
    int ret = EXIT_FAILURE;
    int fd = -1;

    /* prepare for reading the config ini file */
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    char *ini = NULL;
    char *home = strndup(homedir, MAX_DATA_SIZE);
    asprintf(&ini, "%s/%s", home, CONFIG_FILE);

    /* parse config ini file */
    struct Config config;
    memset(&config, 0, sizeof config);

    if (ini_parse(ini, handler, &config) < 0) {
        fprintf(stderr, "Can't load %s. try make install\n", ini);
        syslog(LOG_USER | LOG_ERR, "Can't load %s. Try: make install\n", ini);
        goto cleanup;
    }

   /* get command line options */
    while((opt = getopt_long(argc, argv, optstring, options, &lindex)) != -1) {
        switch(opt) {
            case 'h':
	        usage(EXIT_SUCCESS);
                ret = EXIT_SUCCESS;
                goto cleanup;
                break;
	    case 'u':
                rpc_user = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'r':
                rpc_password = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'i':
                rpc_host = strndup(optarg, MAX_DATA_SIZE);
                break;
	    case 'p':
                rpc_port = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'a':
                account = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'w':
                workdir = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'o':
                notify = atoi(optarg);
                if (notify > UNLOCKED) {
                    syslog(LOG_USER | LOG_ERR, "--notify-at out of range [0,1,2,3]");
                    fprintf(stderr, "mnp: --notify-at out of range [0,1,2,3]\n");
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
                break;
            case 'n':
                confirmation = atoi(optarg);
                break;
            case 'g':
                signature = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'd':
                adr = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 'm':
                message = strndup(optarg, MAX_DATA_SIZE);
                break;
            case 's':
                sp_proof = 1;
                break;
            case 'x':
                tx_proof = 1;
                break;
            case 'R':
                retry = 1;
                break;
            case 't':
                init = 1;
                break;
            case 'c':
                cleanup = 1;
                break;
            case 'v':
                printmnp();
                ret = EXIT_SUCCESS;
                goto cleanup;
                break;
            case 0:
	        break;
            case ':':  /* missing argument */
                fprintf(stderr, "option -%c requires an argument\n", optopt);
                usage(EXIT_FAILURE);
                ret = EXIT_FAILURE;
                goto cleanup;
                break;
            case '?':  /* missing Argument */
                usage(EXIT_SUCCESS);
                ret = EXIT_SUCCESS;
                goto cleanup;
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
            txid = strndup(argv[optind], MAX_TXID_SIZE);
        }
        if (txid == NULL) {
            txid_from_stdin = 1;
            txid = readStdin();
            if (txid == NULL) {
                syslog(LOG_USER | LOG_ERR, "No input data or invalid txid. readStdin");
                fprintf(stderr, "No input data or invalid txid.\n");
                ret = EXIT_FAILURE;
                goto cleanup;
            }
        }

        int valid =  val_hex_input(txid, MAX_TXID_SIZE);
        if(valid < 0) {
                syslog(LOG_USER | LOG_ERR, "No input data or invalid txid. fread: %s", strerror(errno));
                fprintf(stderr, "No input data or invalid txid. fread: %s\n", strerror(errno));
                ret = EXIT_FAILURE;
                goto cleanup;
        }
        
        if (sp_proof == 0 && tx_proof == 0 && !retry) {
            char *mnp_txid_file = NULL;
            asprintf(&mnp_txid_file, "%s/%s", workdir, TMP_TXID_FILE);
            FILE *file = fopen(mnp_txid_file, "a+");
            if (file == NULL) {
                fprintf(stderr, "Error opening tmp file.\n");
                ret = EXIT_FAILURE;
                goto cleanup;
            }

            /* Test, if the current txid is already existing in temp file */
            char line[MAX_TXID_SIZE + 1];
            int txid_found = 0;
            while (fgets(line, sizeof(line), file)) {
                line[strcspn(line, "\n")] = '\0'; // Entferne das Zeilenumbruchzeichen
                if (strncmp(line, txid, MAX_TXID_SIZE) == 0) {
                    txid_found = 1;
                    break;
                }
            }

            /* If the current txid is not found in temp file, add it to the file */
            if (!txid_found) {
                fprintf(file, "%s\n", txid);
                fclose(file);
            } else {
                ret = EXIT_SUCCESS;
                goto cleanup;
            }
        }
    }

    char *perm = strndup(config.cfg_mode, MAX_DATA_SIZE);
    mode_t mode = (((perm[0] == 'r') * 4 | (perm[1] == 'w') * 2 | (perm[2] == 'x')) << 6) |
                  (((perm[3] == 'r') * 4 | (perm[4] == 'w') * 2 | (perm[5] == 'x')) << 3) |
                  (((perm[6] == 'r') * 4 | (perm[7] == 'w') * 2 | (perm[8] == 'x')));

    char *pipe_perm = strndup(config.cfg_pipe, MAX_DATA_SIZE);
    mode_t pmode = (((pipe_perm[0] == 'r') * 4 | (pipe_perm[1] == 'w') * 2 | (pipe_perm[2] == 'x')) << 6) |
                   (((pipe_perm[3] == 'r') * 4 | (pipe_perm[4] == 'w') * 2 | (pipe_perm[5] == 'x')) << 3) |
                   (((pipe_perm[6] == 'r') * 4 | (pipe_perm[7] == 'w') * 2 | (pipe_perm[8] == 'x')));

    if (DEBUG) {
        syslog(LOG_USER | LOG_DEBUG, "mode_t = %03o and mode = %s\n", mode, config.cfg_mode);
        syslog(LOG_USER | LOG_DEBUG, "pipe mode_t = %03o and mode = %s\n", pmode, config.cfg_pipe);
    }

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
        monero_wallet[i].locked = NULL;
        monero_wallet[i].fifo = NULL;
        monero_wallet[i].idx = 0;
        monero_wallet[i].message = NULL;
        monero_wallet[i].signature = NULL;
        monero_wallet[i].proof = NULL;
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
            fprintf(stderr, "mnp: rpc_host is missing\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        if (rpc_port != NULL) {
            monero_wallet[i].port = strndup(rpc_port, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_port is missing");
            fprintf(stderr, "mnp: rpc_port is missing\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        if (rpc_user != NULL) {
            monero_wallet[i].user = strndup(rpc_user, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_user is missing");
            fprintf(stderr, "mnp: rpc_user is missing\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        if (rpc_password != NULL) {
            monero_wallet[i].pwd = strndup(rpc_password, MAX_DATA_SIZE);
        } else {
            syslog(LOG_USER | LOG_ERR, "rpc_password is missing");
            fprintf(stderr, "mnp: rpc_password is missing\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        if (txid != NULL) {
            monero_wallet[i].txid = strndup(txid, MAX_TXID_SIZE);
        } else if (cleanup == 1 || init == 1) {
            monero_wallet[i].txid = strndup("no_tx", MAX_TXID_SIZE);
            if (txid_from_stdin) {
                free(txid);
                txid = NULL;
            }
        } else {
            syslog(LOG_USER | LOG_ERR, "txid is missing\n");
            fprintf(stderr, "mnp: txid is missing\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }
    }

    /* if no account is set - use the default account 0 */
    if (account == NULL) asprintf(&account, "0");

    /*****
     * TEST for --spend-proof OR --tx-proof
     * ****
     */
    if (sp_proof == 1) {
        if (message != NULL) {
            monero_wallet[CHECK_SPEND_PROOF].message = strndup(message, MAX_DATA_SIZE);
        }
        if (signature == NULL) {
            fprintf(stdout, "signature = NULL. --signature option is required using --spend-proof OR --tx-proof\n");
            usage(EXIT_FAILURE);
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        monero_wallet[CHECK_SPEND_PROOF].signature = strndup(signature, MAX_DATA_SIZE);

        int ret2 = 0;
        if (0 > (ret2 = rpc_call(&monero_wallet[CHECK_SPEND_PROOF]))) {
            syslog(LOG_USER | LOG_ERR, "could not connect to host: %s:%s", monero_wallet[CHECK_SPEND_PROOF].host,
                                                                           monero_wallet[CHECK_SPEND_PROOF].port);
            fprintf(stderr, "mnp: could not connect to host: %s:%s\n", monero_wallet[CHECK_SPEND_PROOF].host,
                                                                       monero_wallet[CHECK_SPEND_PROOF].port);

            /* Write txid to rpc_connection_alert pipe on RPC connection error */
            if (workdir && monero_wallet[CHECK_SPEND_PROOF].txid) {
                char *rpc_conn_pipe = NULL;
                char *rpc_conn_content = NULL;
                asprintf(&rpc_conn_pipe, "%s/%s", workdir, RPC_CONN_ALERT);
                asprintf(&rpc_conn_content, "%s", monero_wallet[CHECK_SPEND_PROOF].txid);
                write_to_pipe(rpc_conn_pipe, rpc_conn_content);
                free(rpc_conn_pipe);
                free(rpc_conn_content);
            }
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        monero_wallet[CHECK_SPEND_PROOF].proof = proof(&monero_wallet[CHECK_SPEND_PROOF]);

        if (strcmp(monero_wallet[CHECK_SPEND_PROOF].proof, "true")) {
                fprintf(stdout, "false\n");
                ret = EXIT_FAILURE;
                goto cleanup;
        } else {
                fprintf(stdout, "true\n");
                ret = EXIT_SUCCESS;
                goto cleanup;
        }
    }

    if (tx_proof == 1) {
        if (message != NULL) {
            monero_wallet[CHECK_TX_PROOF].message = strndup(message, MAX_DATA_SIZE);
        }
        if (signature == NULL) {
            fprintf(stdout, "signature = NULL. --signature option is required using --spend-proof OR --tx-proof\n");
            usage(EXIT_FAILURE);
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        if (adr == NULL) {
            fprintf(stdout, "address = NULL. --address option is required using --tx-proof\n");
            usage(EXIT_FAILURE);
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        monero_wallet[CHECK_TX_PROOF].signature = strndup(signature, MAX_DATA_SIZE);
        monero_wallet[CHECK_TX_PROOF].saddr = strndup(adr, MAX_ADDR_SIZE);

        if (DEBUG) fprintf(stdout, "monero_wallet[CHECK_TX_PROOF].txid = %s\n", monero_wallet[CHECK_TX_PROOF].txid);
        if (DEBUG) fprintf(stdout, "monero_wallet[CHECK_TX_PROOF].saddr = %s\n", monero_wallet[CHECK_TX_PROOF].saddr);
        if (DEBUG) fprintf(stdout, "monero_wallet[CHECK_TX_PROOF].signature = %s\n", monero_wallet[CHECK_TX_PROOF].signature);

        int ret2 = 0;
        if (0 > (ret2 = rpc_call(&monero_wallet[CHECK_TX_PROOF]))) {
            syslog(LOG_USER | LOG_ERR, "could not connect to host: %s:%s", monero_wallet[CHECK_TX_PROOF].host,
                                                                           monero_wallet[CHECK_TX_PROOF].port);
            fprintf(stderr, "mnp: could not connect to host: %s:%s\n", monero_wallet[CHECK_TX_PROOF].host,
                                                                       monero_wallet[CHECK_TX_PROOF].port);

            /* Write txid to rpc_connection_alert pipe on RPC connection error */
            if (workdir && monero_wallet[CHECK_TX_PROOF].txid) {
                char *rpc_conn_pipe = NULL;
                char *rpc_conn_content = NULL;
                asprintf(&rpc_conn_pipe, "%s/%s", workdir, RPC_CONN_ALERT);
                asprintf(&rpc_conn_content, "%s", monero_wallet[CHECK_SPEND_PROOF].txid);
                write_to_pipe(rpc_conn_pipe, rpc_conn_content);
                free(rpc_conn_pipe);
                free(rpc_conn_content);
            }
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        monero_wallet[CHECK_TX_PROOF].proof = proof(&monero_wallet[CHECK_TX_PROOF]);
        monero_wallet[CHECK_TX_PROOF].conf = proof_confirm(&monero_wallet[CHECK_TX_PROOF]);
        monero_wallet[CHECK_TX_PROOF].amount = proof_received(&monero_wallet[CHECK_TX_PROOF]);
//      fprintf(stdout,"%s\t%s\n", monero_wallet[CHECK_TX_PROOF].conf, monero_wallet[CHECK_TX_PROOF].amount);

        if (strcmp(monero_wallet[CHECK_TX_PROOF].proof, "true")) {
                ret = EXIT_FAILURE;
                goto cleanup;
        } else {
                ret = EXIT_SUCCESS;
                goto cleanup;
        }
    }

    /*
     * mnp --init
     */
    struct stat sb;
    struct stat transfer;
    asprintf(&txdir, "%s/%s", workdir, TRANSACTION_DIR);

    if (init) {

        /*
         * create work directory /tmp/mywallet
         */
        if (stat(workdir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "workdir does exists : %s", workdir);
        } else {
            int status = mkdir(workdir, mode);
            if (status == -1) {
                syslog(LOG_USER | LOG_ERR, "could not create workdir %s error: %s", workdir, strerror(errno));
                fprintf(stderr, "mnp: could not create workdir %s error: %s \n", workdir, strerror(errno));
                ret = EXIT_FAILURE;
                goto cleanup;
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO, "workdir is up : %s", workdir);
        if (verbose) fprintf(stderr, "workdir is up : %s\n", workdir);

        /*
         * create directory /tmp/mywallet/transactions
         */
        //asprintf(&txdir, "%s/%s", workdir, TRANSACTION_DIR);
        if (stat(txdir, &transfer) == 0 && S_ISDIR(transfer.st_mode)) {
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "txdir does exists : %s", txdir);
        } else {
            int status = mkdir(txdir, mode);
            if (status == -1) {
                syslog(LOG_USER | LOG_ERR, "could not create txdir %s error: %s", txdir, strerror(errno));
                fprintf(stderr, "mnp: could not create txdir %s error: %s\n", txdir, strerror(errno));
                ret = EXIT_FAILURE;
                goto cleanup;
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO, "transactions dir is up : %s", txdir);
        if (verbose) fprintf(stderr, "transactions dir is up : %s\n", txdir);

        /*
         * creat directory /tmp/mywallet/.mnp.txid
         */
        char *mnp_txid_file = NULL;
        asprintf(&mnp_txid_file, "%s/%s", workdir, TMP_TXID_FILE);
        FILE *file = fopen(mnp_txid_file, "a+");
        if (file == NULL) {
            fprintf(stderr, "Error opening tmp file.\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        fclose(file);
        if (verbose) syslog(LOG_USER | LOG_INFO, ".mnp.txid file is up : %s", mnp_txid_file);
        if (verbose) fprintf(stderr, ".mnp.txid file is up : %s\n", mnp_txid_file);

        /*
         * create /tmp/mywallet/txid
         */
        asprintf(&txid_pipe, "%s/%s", workdir, TXID_PIPE);

        int exist = 0;
        if (stat(txid_pipe, &sb) == 0 && S_ISFIFO(sb.st_mode)) {
            /* file does exist. */
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe txid does exists : %s", txid_pipe);
            exist = 1;
        }
       /* file does NOT exist. */
        if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe txid is created : %s", txid_pipe);
        if (notify != NONE && exist == 0) {
            int retfifo = mkfifo(txid_pipe, pmode);
            if (retfifo == -1) {
                syslog(LOG_USER | LOG_ERR, "could not create named pipe fifo %s error: %s", txid_pipe, strerror(errno));
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO, "named pipe txid is up : %s", txid_pipe);
        if (verbose) fprintf(stderr, "named pipe txid is up : %s\n", txid_pipe);

        /*
         * create /tmp/mywallet/double_spend_alert
         */
        asprintf(&double_spend_pipe, "%s/%s", workdir, DS_ALERT_PIPE);

        exist = 0;
        if (stat(double_spend_pipe, &sb) == 0 && S_ISFIFO(sb.st_mode)) {
            /* file does exist. */
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe double_spend_alert does exists : %s", double_spend_pipe);
            exist = 1;
        }
       /* File does NOT exist. */
        if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe double_spend_alert is created : %s", double_spend_pipe);
        if (notify != NONE && exist == 0) {
            int retfifo = mkfifo(double_spend_pipe, pmode);
            if (retfifo == -1) {
                syslog(LOG_USER | LOG_ERR, "could not create named pipe fifo %s error: %s", double_spend_pipe, strerror(errno));
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO, "named pipe double_spend_alert is up : %s", double_spend_pipe);
        if (verbose) fprintf(stderr, "named pipe double_spend_alert is up : %s\n", double_spend_pipe);

        /*
         * create /tmp/mywallet/rpc_connection_alert
         */
        asprintf(&rpc_conn_alert_pipe, "%s/%s", workdir, RPC_CONN_ALERT);
        exist = 0;
        if (stat(rpc_conn_alert_pipe, &sb) == 0 && S_ISFIFO(sb.st_mode)) {
            /* file does exist. */
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG,
                    "named pipe rpc_connection_alert does exists : %s",
                    rpc_conn_alert_pipe);
            exist = 1;
        }
        /* File does NOT exist. */
        if (DEBUG) syslog(LOG_USER | LOG_DEBUG,
                "named pipe rpc_connection_alert is created : %s",
                rpc_conn_alert_pipe);
        if (notify != NONE && exist == 0) {
            int retfifo = mkfifo(rpc_conn_alert_pipe, pmode);
            if (retfifo == -1) {
                syslog(LOG_USER | LOG_ERR,
                        "could not create named pipe fifo %s error: %s",
                        rpc_conn_alert_pipe, strerror(errno));
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO,
                "named pipe rpc_connection_alert is up : %s", rpc_conn_alert_pipe);
        if (verbose) fprintf(stderr,
                "named pipe rpc_connection_alert is up : %s\n", rpc_conn_alert_pipe);

        exit(EXIT_SUCCESS);
    }

    /*
     * mnp --cleanup
     */
    if (cleanup) {
        int retc = remove_directory(txdir);
        if (retc == -1) {
            syslog(LOG_USER | LOG_ERR, "could not del txdir %s error: %s", txdir, strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            if (verbose) syslog(LOG_USER | LOG_INFO, "transactions dir is down %s", txdir);
            if (verbose) fprintf(stderr, "transactions dir is down %s\n", txdir);
        }

        retc = remove_directory(workdir);
        if (retc == -1) {
            syslog(LOG_USER | LOG_ERR, "could not del workdir %s error: %s", workdir, strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            if (verbose) syslog(LOG_USER | LOG_INFO, "workdir is down %s", workdir);
            if (verbose) fprintf(stderr, "workdir is down %s\n", workdir);
        }
        exit(EXIT_SUCCESS);
    }

    /*
     * mnp --notify-at 0
     */
    if (notify == NONE) {
        ret = EXIT_SUCCESS;
        goto cleanup;
    }

    /*
     * JAIL starts here
     */
    int jail = 1;
    running = jail;
    cJSON *transfers = NULL;

    while (running) {
        int retcall = -1;
        if (0 > (retcall = rpc_call(&monero_wallet[GET_TXID]))) {
            syslog(LOG_USER | LOG_ERR, "could not connect to host: %s:%s", monero_wallet[GET_TXID].host,
                                                                           monero_wallet[GET_TXID].port);
            fprintf(stderr, "mnp: could not connect to host: %s:%s\n", monero_wallet[GET_TXID].host,
                                                                       monero_wallet[GET_TXID].port);

            /* Write txid to rpc_connection_alert pipe on RPC connection error */
            if (workdir && monero_wallet[GET_TXID].txid) {
                char *rpc_conn_pipe = NULL;
                char *rpc_conn_content = NULL;
                asprintf(&rpc_conn_pipe, "%s/%s", workdir, RPC_CONN_ALERT);
                asprintf(&rpc_conn_content, "%s", monero_wallet[GET_TXID].txid);
                write_to_pipe(rpc_conn_pipe, rpc_conn_content);
                free(rpc_conn_pipe);
                free(rpc_conn_content);
            }
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        transfers = get_transfers(&monero_wallet[GET_TXID]);
        if (transfers == NULL) {
            ret = EXIT_FAILURE;
            fprintf(stderr, "ERROR RESPONSE\n");
            syslog(LOG_USER | LOG_ERR, "mnp ERROR");
            goto cleanup;
        }
        const cJSON *trans = cJSON_GetArrayItem(transfers, 0);

        switch(notify) {
            case TXPOOL:
                if (DEBUG) fprintf(stderr, "Amount of Transfers: %d\n", cJSON_GetArraySize(transfers));
                jail = 0;
                break;
            case CONFIRMED:
                jail = 1;
                char *conf = get_confirm(trans);
                if (conf == NULL) asprintf(&conf, "0");
                if (atoi(conf) >= confirmation) {
                    jail = 0;
                }
                break;
            case UNLOCKED:
                jail = 1;
                char *locked = get_locked(trans);
                if (strncmp(locked, "false", MAX_DATA_SIZE) == 0) {
                    jail = 0;
                }
                break;
            default:
                syslog(LOG_USER | LOG_ERR, "Error, check --notify-at x\n");
                fprintf(stderr, "mnp: error, check --notify-at x\n");
                ret = EXIT_FAILURE;
                goto cleanup;
    	    break;
        }

        sleep(poll_interval);
        running = jail;
    }

    /*
     * mkdir /tmp/mywallet/transactions/txid/
     */
    asprintf(&txId, "%s/%s/%s", workdir, TRANSACTION_DIR, monero_wallet[GET_TXID].txid);
    if (stat(txId, &transfer) == 0 && S_ISDIR(transfer.st_mode)) {
        if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "txId does exists : %s", txId);
    } else {
        int status = mkdir(txId, mode);
        if (status == -1) {
            syslog(LOG_USER | LOG_ERR, "could not create txId %s error: %s", txId, strerror(errno));
            fprintf(stderr, "mnp: could not create txId %s error: %s\n", txId, strerror(errno));
            ret = EXIT_FAILURE;
            goto cleanup;
        }
    }
    if (verbose) syslog(LOG_USER | LOG_INFO, "txId is up : %s", txId);
    if (verbose) fprintf(stderr, "txId is up : %s\n", txId);

    const cJSON *trans = NULL;

    /* loop through every transfer in the list transfers */
    cJSON_ArrayForEach(trans, transfers) {
        char *fifo = NULL;
        cJSON *address = cJSON_GetObjectItem(trans, "address");
        if (address == NULL) exit(EXIT_FAILURE);
        cJSON *payment_id = cJSON_GetObjectItem(trans, "payment_id");
        if (payment_id == NULL) exit(EXIT_FAILURE);
        cJSON *amount = cJSON_GetObjectItem(trans, "amount");
        if (amount == NULL) exit(EXIT_FAILURE);
        cJSON *double_spend = cJSON_GetObjectItem(trans, "double_spend_seen");
        if (double_spend == NULL) exit(EXIT_FAILURE);

        char *cur_address = delQuotes(cJSON_Print(address));
        char *cur_payment_id = delQuotes(cJSON_Print(payment_id));
        char *cur_amount = cJSON_Print(amount);
        char *cur_double_spend = cJSON_Print(double_spend);

        char *adrorpay = NULL;
        if (strcmp(cur_payment_id, PAYNULL)) {
            asprintf(&adrorpay, "%s", cur_payment_id);
            asprintf(&fifo, "%s/%s", txId, cur_payment_id);
        } else {
            asprintf(&adrorpay, "%s", cur_address);
            asprintf(&fifo, "%s/%s", txId, cur_address);
        }

        if (strncmp(cur_double_spend, "true", MAX_DATA_SIZE) == 0) {
            char *double_spend_content = NULL;
            asprintf(&double_spend_pipe, "%s/%s", workdir, DS_ALERT_PIPE);
            asprintf(&double_spend_content, "%s %s", txid, adrorpay);
            write_to_pipe(double_spend_pipe, double_spend_content);
        }

        /*
         * mkfifo named pipe
         */
        int exist = 0;
        if (stat(fifo, &sb) == 0 && S_ISFIFO(sb.st_mode)) {
            /* file does exist. second call */
            if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe fifo does exists : %s", fifo);
            exist = 1;
        }
       /* file does NOT exist. */
        if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "named pipe fifo is created : %s", fifo);
        if (notify != NONE && exist == 0) {
            int retfifo = mkfifo(fifo, pmode);
            if (retfifo == -1) {
                syslog(LOG_USER | LOG_ERR, "could not create named pipe fifo %s error: %s", fifo, strerror(errno));
            }
        }
        if (verbose) syslog(LOG_USER | LOG_INFO, "named pipe fifo is up : %s", fifo);
        if (verbose) fprintf(stderr, "named pipe fifo is up : %s\n", fifo);

        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_USER | LOG_ERR, "error: fork: %s", strerror(errno));
            fprintf(stderr, "mnp: error: fork: %s\n", strerror(errno));
            ret = EXIT_FAILURE;
            goto cleanup;
        } else if (pid == 0) {
            /* Child process: write to named pipe */
            if (fifo && strlen(fifo) > 0) {
                fd = open(fifo, O_WRONLY | O_CLOEXEC);
                 if (fd == -1) {
                    syslog(LOG_USER | LOG_ERR, "error: open fifo %s: %s", fifo, strerror(errno));
                    fprintf(stderr, "mnp: error: open fifo %s: %s\n", fifo, strerror(errno));
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
            }

            ssize_t retw = dprintf(fd, "%s\n", cur_amount);
            if (retw == -1) {
                syslog(LOG_USER | LOG_ERR, "error: write %s", strerror(errno));
                fprintf(stderr, "mnp: error: %s", strerror(errno));
                ret = EXIT_FAILURE;
                goto cleanup;
            }

            if (unlink(fifo) == -1) {
                syslog(LOG_USER | LOG_ERR, "error: unlink fifo %s: %s", fifo, strerror(errno));
                fprintf(stderr, "mnp: error: unlink fifo %s: %s\n", fifo, strerror(errno));
                ret = EXIT_FAILURE;
                goto cleanup;
            }

            close(fd);
            ret = EXIT_SUCCESS;
            goto cleanup;
        } else {
            /* Partent process continue the loop */
            if (DEBUG) fprintf(stderr, "Parent process continues. Child PID: %d\n", pid);

            char *txid_content = NULL;
            asprintf(&txid_content, "%s %s", txid, adrorpay);
            asprintf(&txid_pipe, "%s/%s", workdir, TXID_PIPE);
            write_to_pipe(txid_pipe, txid_content);

            /*Free the fifo variable to avoid potential memory leaks */
            free(fifo);
        }
    }

cleanup:
    //if (transfers != NULL) cJSON_Delete(transfers);
    if (fd >= 0) close(fd);
    if (monero_wallet != NULL) free(monero_wallet);
    if (txid && txid_from_stdin) free(txid);
    if (home) free(home);
    if (ini) free(ini);
    closelog();
    exit(ret);
}


/**
 * write_to_pipe - Write a message into a named pipe (FIFO).
 *
 * @pipe:    Path to the pipe (e.g. "/tmp/mypipe").
 * @content: Message to write.
 *
 * Opens the given FIFO for writing and sends the message.
 * Blocks until a reader is connected. Prints an error if the
 * pipe cannot be opened or written to.
 */
void write_to_pipe(const char *pipe, const char *content) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_USER | LOG_ERR, "error: fork write_to_pipe: %s - %s", pipe, strerror(errno));
        fprintf(stderr, "error: fork write_to_pipe: %s - %s", pipe, strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Child process: write to named pipe */
        int fd = -1;
        if (pipe && strlen(pipe) > 0) {
            fd = open(pipe, O_WRONLY | O_CLOEXEC);
            if (fd == -1) {
                syslog(LOG_USER | LOG_ERR, "error open: write to pipe %s: %s", pipe, strerror(errno));
                fprintf(stderr, "mnp: error open:  write to pipe %s: %s\n", pipe, strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        ssize_t retw = dprintf(fd, "%s\n", content);
            if (retw == -1) {
            syslog(LOG_USER | LOG_ERR, "error: write_to_pipe %s", strerror(errno));
            fprintf(stderr, "mnp: error: write_to_pipe %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
}


/**
 * Retrieves the list of transfers from the Monero wallet RPC response.
 *
 * This function extracts the list of transfers, each representing a transaction or
 * part of a transaction, including details such as address, payment ID, and amount.
 * This information is essential for processing and analyzing transactions.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A pointer to a cJSON array containing the list of transfers, or NULL if the extraction fails.
 */
static cJSON *get_transfers(struct rpc_wallet *monero_wallet) {
    assert (monero_wallet != NULL);

    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    if (result == NULL) {
        fprintf(stderr, "result\n");
        return NULL;
    }

    cJSON *transfers = cJSON_GetObjectItem(result, "transfers");
    if (transfers == NULL) {
        fprintf(stderr, "transfers\n");
        return NULL;
    }
    return transfers;
}


/**
 * Extracts the number of confirmations from the Monero wallet RPC response.
 *
 * This function retrieves the number of confirmations for a transaction, indicating
 * the number of blocks added to the blockchain since the transaction was included.
 * A higher number of confirmations generally indicates a more secure transaction.
 *
 * @param transfer A pointer to the cJSON object representing the transfer.
 * @return A dynamically allocated string containing the number of confirmations, or NULL if the extraction fails.
 */
static char *get_confirm(const cJSON *transfer)
{
    assert (transfer != NULL);

    cJSON *confirmations = cJSON_GetObjectItem(transfer, "confirmations");

    return cJSON_Print(confirmations);
}


/**
 * Extracts the locked status from the Monero wallet RPC response.
 *
 * This function retrieves the locked status of a transaction, indicating whether
 * the transaction is currently locked or unlocked. This is crucial for determining
 * the availability of funds for spending.
 *
 * @param transfer A pointer to the cJSON object representing the transfer.
 * @return A dynamically allocated string containing the locked status, or NULL if the extraction fails.
 */
static char *get_locked(const cJSON *transfer)
{
    assert (transfer != NULL);

    cJSON *locked = cJSON_GetObjectItem(transfer, "locked");

    return cJSON_Print(locked);
}


/**
 * Extracts the signiture (good) status from the Monero wallet RPC response.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A dynamically allocated string containing the siginture status, or NULL if the extraction fails.
 */
static char *proof(const struct rpc_wallet *monero_wallet)
{
    assert (monero_wallet != NULL);

    /* TODO: TEST for != NULL */
    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *good = cJSON_GetObjectItem(result, "good");

    return cJSON_Print(good);
}


/**
 * Extracts the confirmations from the Monero wallet RPC response.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A dynamically allocated string containing the amount of  confirmation, or NULL if the extraction fails.
 */
static char *proof_confirm(const struct rpc_wallet *monero_wallet)
{
    assert (monero_wallet != NULL);

    /* TODO: TEST for != NULL */
    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *confirmations = cJSON_GetObjectItem(result, "confirmations");

    return cJSON_Print(confirmations);
}


/**
 * Extracts the received amount of piconerp from the Monero wallet RPC response.
 *
 * @param monero_wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A dynamically allocated string containing the amount of piconero received, or NULL if the extraction fails.
 */
static char *proof_received(const struct rpc_wallet *monero_wallet)
{
    assert (monero_wallet != NULL);

    /* TODO: TEST for != NULL */
    cJSON *result = cJSON_GetObjectItem(monero_wallet->reply, "result");
    cJSON *amount = cJSON_GetObjectItem(result, "received");

    return cJSON_Print(amount);
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
    "Usage: mnp [OPTION] [TXID]\n\n"
    "      [TX_ID]\n"
    "               TXID is the transaction identifier passed from\n"
    "               monero-wallet-rpc --tx-notify or some other source.\n\n"
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
    "  -w, --workdir [WORKDIR]\n"
    "               place to create the work directory.\n\n"
    "      --notify-at [0,1,2,3] default = confirmed\n"
    "               0, none\n"
    "               1, txpool\n"
    "               2, confirmed\n"
    "               3, unlocked\n\n"
    "      --confirmation [n] default = 1\n"
    "               amount of blocks needed to confirm transaction.\n\n"
    "      ########################################################\n\n"
    "      --spend-proof\n"
    "               check spend proof. SIGNATURE is required.\n\n"
    "      --tx-proof\n"
    "               check transaction proof.\n"
    "               SIGNATURE and ADDRESS is required.\n\n"
    "      --signature [SIGNATURE]\n"
    "               required by --tx-proof or --spend-proof.\n\n"
    "      --address [ADDRESS]\n"
    "               required by --tx-proof.\n\n"
    "      --message [MESSAGE]\n"
    "               optional used by --tx-proof or --spend-proof.\n\n"
    "      ########################################################\n\n"
    "      --init\n"
    "               create workdir for usage.\n\n"
    "      --cleanup\n"
    "               delete workdir.\n\n"
    "      --retry\n"
    "               if rpc_connection_alert is triggered - use retry.\n\n"
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


/**
 * Reads the transaction ID from standard input.
 *
 * @return A dynamically allocated string containing the
 * transsaction ID read from stdin, or NULL if an error occurs.
 */
static char *readStdin(void)
{
    char *buffer;

    buffer = (char *)malloc(MAX_TXID_SIZE+1);
    if(buffer == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    size_t ret = fread(buffer, 1, MAX_TXID_SIZE, stdin);
    if(ret != MAX_TXID_SIZE) {
        return NULL;
    }

    buffer[ret] = '\0';

    return buffer;
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


int get_env_int(const char *name, int fallback) {
    char *val = getenv(name);
    if (val && *val) {
        char *end;
        long i = strtol(val, &end, 10);
        if (*end == '\0') { // nur wenn es wirklich eine Zahl ist
            return (int)i;
        }
    }
    return fallback;
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
 * Callback function for removing files and directories.
 *
 * @param fpath The path of the file or directory to be removed.
 * @param sb The stat structure containing information about the file or directory.
 * @param typeflag The type of the file or directory (file, directory, etc.).
 * @param ftwbuf Additional information about the file or directory.
 * @return 0 on success, -1 on failure.
 */
static int remove_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (remove(fpath) == -1) {
        perror("remove");
        return -1;
    }
    return 0;
}


/**
 * Recursively removes a work directory and its pipes.
 * Uses file descriptors to mitigate TOCTOU race condition.
 *
 * Parameters:
 *   - path: Path of the directory to be removed.
 *
 * Returns:
 *   - 0 on success, -1 on failure.
 */
static int remove_directory(const char *path) {
    if (nftw(path, remove_callback, 64, FTW_DEPTH | FTW_PHYS) == -1) {
        perror("nftw");
        return -1;
    }
    return 0;
}


/**
 * Prints the version information of Monero Named Pipes.
 */
static void printmnp(void)
{
                printf(ANSI_RESET_ALL
                MONERO_ORANGE "Monero "
                MONERO_GREY "Named Pipes | "
                ANSI_RESET_ALL "mnp Version %s\n", VERSION);
}
