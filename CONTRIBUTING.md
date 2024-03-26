## Style guide

* A line of source code should not exceed 120 characters,
* indent/tab should consists of 4 whitespaces.
  
Comments should be made in the following way:

```c
/*
 * My comment
 * returns x
 */

/* My comment */
```

Try to avoid ```// My comment```.

## Error handling / output

- Every error should be printed to stderr,
- User information should be printed to stdout.

Use syslog where it makes sense to do so:

- LOG_ERR: use this, side by side with every error printed to stderr,
- LOG_INFO: use this for verbose output. If --verbose option is set,
- LOG_DEBUG: use this for DEBUG messages. #define DEBUG (1) set in globaldefs.h

```c
/* Error */
syslog(LOG_USER | LOG_ERR, "txid is missing\n");
fprintf(stderr, "mnp: txid is missing\n");

/* Info */
if (verbose) syslog(LOG_USER | LOG_INFO, "workdir is up : %s", workdir);

/* Debug */
if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "%d bytes received", ret);
```
Use a unique identifier when using openlog:
```c
mnp.c:      openlog("mnp:", LOG_PID, LOG_USER);
rpc_call.c: openlog("mnp:rpc_call:", LOG_PID, LOGUSER);
```
