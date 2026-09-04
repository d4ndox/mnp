/* ============================================================
 * cleanup.c
 * ============================================================ */

#define _XOPEN_SOURCE 700

#include "cleanup.h"

#include <errno.h>
#include <ftw.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

    if (strcmp(section, "cfg") == 0 &&
        strcmp(name, "workdir") == 0) {
        config->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    }

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

static int remove_callback(
    const char *path,
    const struct stat *status,
    int type,
    struct FTW *info
)
{
    (void)status;
    (void)type;
    (void)info;

    if (remove(path) == -1) {
        fprintf(
            stderr,
            "mnp cleanup: cannot remove '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

static int remove_directory(const char *path)
{
    struct stat status;

    if (lstat(path, &status) == -1) {
        if (errno == ENOENT) {
            return 0;
        }

        fprintf(
            stderr,
            "mnp cleanup: cannot inspect '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (!S_ISDIR(status.st_mode)) {
        fprintf(
            stderr,
            "mnp cleanup: '%s' is not a directory\n",
            path
        );
        return -1;
    }

    if (nftw(path, remove_callback, 64, FTW_DEPTH | FTW_PHYS) == -1) {
        return -1;
    }

    return 0;
}

void cleanup_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s cleanup\n"
        "\n"
        "Remove the mnp working directory and all files and FIFOs "
        "inside it.\n",
        program
    );
}

int cleanup_main(int argc, char **argv)
{
    struct Config config = {0};
    char *ini_path = NULL;
    int result = EXIT_FAILURE;

    if (argc != 1) {
        fprintf(
            stderr,
            "mnp cleanup: unexpected argument '%s'\n"
            "Try '%s help' for usage.\n",
            argv[1],
            argv[0]
        );
        return EXIT_FAILURE;
    }

    ini_path = config_path();

    if (ini_path == NULL) {
        fprintf(stderr, "mnp cleanup: cannot determine config path\n");
        goto done;
    }

    if (ini_parse(ini_path, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp cleanup: cannot load configuration '%s'\n",
            ini_path
        );
        goto done;
    }

    if (config.cfg_workdir == NULL ||
        config.cfg_workdir[0] == '\0' ||
        strcmp(config.cfg_workdir, "/") == 0) {
        fprintf(stderr, "mnp cleanup: invalid cfg.workdir\n");
        goto done;
    }

    if (remove_directory(config.cfg_workdir) == -1) {
        goto done;
    }

    result = EXIT_SUCCESS;

done:
    free((void *)config.cfg_workdir);
    free(ini_path);

    return result;
}
