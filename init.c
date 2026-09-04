/* ============================================================
 * init.c
 * ============================================================ */

#include "init.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "globaldefs.h"
#include "inih/ini.h"

static int config_handler(
    void *user,
    const char *section,
    const char *name,
    const char *value
)
{
    struct Config *config = user;

#define MATCH(s, n) \
    (strcmp(section, (s)) == 0 && strcmp(name, (n)) == 0)

    if (MATCH("cfg", "workdir")) {
        config->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "mode")) {
        config->cfg_mode = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "pipe")) {
        config->cfg_pipe = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "verbose")) {
        config->mnp_verbose = strndup(value, MAX_DATA_SIZE);
    }

#undef MATCH

    return 1;
}

static char *config_path(void)
{
    const char *home;
    char *path = NULL;

    home = getenv("HOME");

    if (home == NULL) {
        const struct passwd *entry = getpwuid(getuid());

        if (entry == NULL || entry->pw_dir == NULL) {
            return NULL;
        }

        home = entry->pw_dir;
    }

    if (asprintf(&path, "%s/%s", home, CONFIG_FILE) == -1) {
        return NULL;
    }

    return path;
}

static int parse_permissions(const char *permissions, mode_t *mode)
{
    mode_t result = 0;

    if (permissions == NULL ||
        mode == NULL ||
        strlen(permissions) != 9) {
        return -1;
    }

    for (size_t i = 0; i < 9; ++i) {
        const char expected;

        switch (i % 3) {
        case 0:
            expected = 'r';
            break;
        case 1:
            expected = 'w';
            break;
        default:
            expected = 'x';
            break;
        }

        if (permissions[i] != expected && permissions[i] != '-') {
            return -1;
        }

        if (permissions[i] == expected) {
            result |= (mode_t)1 << (8 - i);
        }
    }

    *mode = result;

    return 0;
}

static int ensure_directory(const char *path, mode_t mode)
{
    struct stat status;

    if (stat(path, &status) == 0) {
        if (!S_ISDIR(status.st_mode)) {
            fprintf(
                stderr,
                "mnp init: '%s' exists but is not a directory\n",
                path
            );
            return -1;
        }

        return 0;
    }

    if (errno != ENOENT) {
        fprintf(
            stderr,
            "mnp init: cannot inspect '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (mkdir(path, mode) == -1) {
        fprintf(
            stderr,
            "mnp init: cannot create directory '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

static int ensure_file(const char *path)
{
    FILE *file;

    file = fopen(path, "a");

    if (file == NULL) {
        fprintf(
            stderr,
            "mnp init: cannot create file '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (fclose(file) == EOF) {
        fprintf(
            stderr,
            "mnp init: cannot close file '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

static int ensure_fifo(const char *path, mode_t mode)
{
    struct stat status;

    if (lstat(path, &status) == 0) {
        if (!S_ISFIFO(status.st_mode)) {
            fprintf(
                stderr,
                "mnp init: '%s' exists but is not a FIFO\n",
                path
            );
            return -1;
        }

        return 0;
    }

    if (errno != ENOENT) {
        fprintf(
            stderr,
            "mnp init: cannot inspect '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (mkfifo(path, mode) == -1) {
        fprintf(
            stderr,
            "mnp init: cannot create FIFO '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

static void free_config(struct Config *config)
{
    if (config == NULL) {
        return;
    }

    free((void *)config->cfg_workdir);
    free((void *)config->cfg_mode);
    free((void *)config->cfg_pipe);
    free((void *)config->mnp_verbose);
}

void init_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s init\n"
        "\n"
        "Initialize the mnp working directory.\n"
        "\n"
        "Creates:\n"
        "  WORKDIR/\n"
        "  WORKDIR/%s/\n"
        "  WORKDIR/%s\n"
        "  WORKDIR/%s\n"
        "  WORKDIR/%s\n"
        "  WORKDIR/%s\n",
        program,
        TRANSACTION_DIR,
        TMP_TXID_FILE,
        TXID_PIPE,
        DS_ALERT_PIPE,
        RPC_CONN_ALERT
    );
}

int init_main(int argc, char **argv)
{
    struct Config config = {0};
    char *ini_path = NULL;
    char *transactions = NULL;
    char *txid_file = NULL;
    char *txid_fifo = NULL;
    char *double_spend_fifo = NULL;
    char *rpc_alert_fifo = NULL;
    mode_t directory_mode;
    mode_t fifo_mode;
    int result = EXIT_FAILURE;

    if (argc != 1) {
        fprintf(
            stderr,
            "mnp init: unexpected argument '%s'\n"
            "Try '%s help' for usage.\n",
            argv[1],
            argv[0]
        );
        return EXIT_FAILURE;
    }

    ini_path = config_path();

    if (ini_path == NULL) {
        fprintf(stderr, "mnp init: cannot determine config path\n");
        goto done;
    }

    if (ini_parse(ini_path, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp init: cannot load configuration '%s'\n",
            ini_path
        );
        goto done;
    }

    if (config.cfg_workdir == NULL) {
        fprintf(stderr, "mnp init: cfg.workdir is missing\n");
        goto done;
    }

    if (parse_permissions(config.cfg_mode, &directory_mode) == -1) {
        fprintf(stderr, "mnp init: invalid cfg.mode\n");
        goto done;
    }

    if (parse_permissions(config.cfg_pipe, &fifo_mode) == -1) {
        fprintf(stderr, "mnp init: invalid cfg.pipe\n");
        goto done;
    }

    if (asprintf(
            &transactions,
            "%s/%s",
            config.cfg_workdir,
            TRANSACTION_DIR
        ) == -1 ||
        asprintf(
            &txid_file,
            "%s/%s",
            config.cfg_workdir,
            TMP_TXID_FILE
        ) == -1 ||
        asprintf(
            &txid_fifo,
            "%s/%s",
            config.cfg_workdir,
            TXID_PIPE
        ) == -1 ||
        asprintf(
            &double_spend_fifo,
            "%s/%s",
            config.cfg_workdir,
            DS_ALERT_PIPE
        ) == -1 ||
        asprintf(
            &rpc_alert_fifo,
            "%s/%s",
            config.cfg_workdir,
            RPC_CONN_ALERT
        ) == -1) {
        fprintf(stderr, "mnp init: memory allocation failed\n");
        goto done;
    }

    if (ensure_directory(config.cfg_workdir, directory_mode) == -1 ||
        ensure_directory(transactions, directory_mode) == -1 ||
        ensure_file(txid_file) == -1 ||
        ensure_fifo(txid_fifo, fifo_mode) == -1 ||
        ensure_fifo(double_spend_fifo, fifo_mode) == -1 ||
        ensure_fifo(rpc_alert_fifo, fifo_mode) == -1) {
        goto done;
    }

    result = EXIT_SUCCESS;

done:
    free(rpc_alert_fifo);
    free(double_spend_fifo);
    free(txid_fifo);
    free(txid_file);
    free(transactions);
    free(ini_path);
    free_config(&config);

    return result;
}

