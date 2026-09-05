/*
 * Copyright (c) 2026 d4ndo@proton.me
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

static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *config_path(void);
static int remove_callback(const char *path, const struct stat *status, int type, struct FTW *info);
static int remove_directory(const char *path);

/**
 * Removes the configured mnp working directory and all contained files.
 *
 * The command loads cfg.workdir from the mnp configuration and recursively
 * removes the directory tree without following symbolic links.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE if cleanup fails.
 */
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

/**
 * Prints usage information for the cleanup command.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in the usage example.
 */
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

/**
 * Parses the working directory from the mnp configuration file.
 *
 * @param user A pointer to the Config structure receiving the parsed value.
 * @param section The configuration section name.
 * @param name The configuration option name.
 * @param value The configuration option value.
 * @return 1 after processing the configuration entry.
 */
static int config_handler(void *user, const char *section, const char *name, const char *value)
{
    struct Config *config = user;

    if (strcmp(section, "cfg") == 0 &&
        strcmp(name, "workdir") == 0) {
        config->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    }

    return 1;
}

/**
 * Builds the path to the mnp configuration file.
 *
 * @return A dynamically allocated string containing the configuration path,
 *         or NULL if the path cannot be created.
 */
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

/**
 * Removes a filesystem entry visited by nftw().
 *
 * Entries are removed after their children because remove_directory() uses
 * FTW_DEPTH.
 *
 * @param path The filesystem path of the entry to remove.
 * @param status A pointer to the entry's stat structure.
 * @param type The nftw() entry type.
 * @param info A pointer to traversal information supplied by nftw().
 * @return 0 on success, or -1 if the entry cannot be removed.
 */
static int remove_callback(const char *path, const struct stat *status, int type, struct FTW *info)
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

/**
 * Recursively removes a directory tree.
 *
 * Symbolic links are not followed. A missing directory is treated as an
 * already completed cleanup.
 *
 * @param path The filesystem path of the directory to remove.
 * @return 0 on success, or -1 if the path is invalid or removal fails.
 */
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
