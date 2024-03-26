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

syslog is necessary if a process can't access the terminal ttyS0.
This might be the case for mnp, because it is called by monero_wallet_rpc and mnpd, which is run as a daemon.
Output to stdout and stderr might get lost. For this case it is usefull to 
write messages to a log file.

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
if (DEBUG) syslog(LOG_USER | LOG_DEBUG, "mode_t = %03o and mode = %s\n", mode, config.cfg_mode);
```
Use a unique identifier when using openlog:
```c
mnp.c:      openlog("mnp:", LOG_PID, LOG_USER);
rpc_call.c: openlog("mnp:rpc_call:", LOG_PID, LOGUSER);
```
