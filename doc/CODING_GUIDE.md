# Coding Guide for Monero Named Pipe

Language:  'Gnu C Standard C99'

Using Cmake + Make to build the target.

Libraries used:

* *libcurl* (linked)

* *ini* (dirctory ini) https://github.com/benhoyt/inih

* *cjson* (directory cjson) https://github.com/DaveGamble/cJSON

  

## Build/Compile:

Debug :

```bash
$ cmake -DCMAKE_BUILD_TYPE=Debug ../
$ make
$ make install
```

Release :


```bash
$ cmake -DCMAKE_BUILD_TYPE=Release ../
$ make
$ make install
```



## General Info

Monero Named Pipes uses the »**monero-wallet-rpc**« API to communicate with the Monero wallet. A list of all rpc calls can be found here: 

https://www.getmonero.org/resources/developer-guides/wallet-rpc.html

```
+---------+             +-------------------+              +-----+
|         | +---------> |                   | +----------> |     |
| monerod | 18081/18082 | monero wallet rpc |  18083 rpc   | mnp |
|         | <---------+ |                   | <----------+ |     |
+----+----+             +-------------------+              +--+--+
     |                  |                                     |
     v                  |  +-------------+                    v
   +-+-+                |  |             |                 +mymonero
   |   |                +->+  My wallet  |                 |
   +-+-+                   |             |                 +-+total-balance
     |                     +-------------+                 |
   +-+-+                                                   +-+payments
   |   |                                                   | |
   +-+-+                                                   | +-+paymentID1
     |                                                     | |
                                                           | +-+paymentID2
 Blockchain                                                | 
                                                           ...

```

1) **monerod**. Keeps the blockchain in sync,

2) **monero-wallet-rpc**. Listens on rpc port and takes care of your wallet,

3) **mnp-payment** or **mnp** talk to monero_wallet_rpc.



## File Structure:

* *mnpd.c* 

  main soucre code file for the target »mnpd«

  mnp daemon is »To be defined«,

* *mnp.c* 

  main source code file for the target »mnp«.

  using rpc_call 

  creates the named pipe

  tests for confirmation

  returns ouput to named pipe.,

* *mnp-payment.c*

  main source code file for the target »mnp-payment«

  using rpc_call with different method calls - 

  according to command line option set by the user.

  returns output to stdout,

* *rpc_call.c*

  prepare the RPC JSON string with according method call.

  Calls the function ```wallet``` from *wallet.c*,

* *wallet.c*

  communicate with »monero_wallet_rpc« using curl,

* *globaldefs.h*

  global macros used by every c file,

* *delQuotes.c*

  helper function to remove quotes from a C string,

* *version.c* 

  not used at the moment. It asks for the current RPC version

  used by »monero_wallet_rpc«.



![Monero Named Pipe](https://raw.githubusercontent.com/d4ndox/mnp/master/doc/file_structure_coding_guide.svg)


## Data Structure:

The most important data structure is ```struct rpc_wallet``` to be found in *rpc_call.h*. This structure has two tasks. 

1) Prepare a connection to ===> **monero_wallet_rpc**

   parameter like:

   * ```int monero_rpc_method```,

   * ```char host```, 
   * ```char port```,
   * ```char user```, 
   * ```char pwd``` are necessary for this call by *rpc_call.c*.

2) Store the reply of <=== **monero_wallet_rpc**

   pointer to reply data: ```cJSON *reply```.


```c
/* rpc_call.h */
struct rpc_wallet {
        int monero_rpc_method;
        char *params;
        char *account;
        char *host;
        char *port;
        char *user;
        char *pwd;
        /* tx related */
        char *txid;
        char *payid;
        char *saddr;
        char *iaddr;
        char *amount;
        char *conf;
        char *fifo;
        int   idx;
        cJSON *reply;
};
````

reply is a JSON formated string and needs to be parsed from *mnp.c* or *mnp-payment.c* using the cJSON library.

The result can be stored in the parameter below according to which  ```monero_rpc_method``` method was used:

* ```char txid,```
* ```char payid,```
* ```char saddr,```
* ..
* ```int idx```.

`int monero_rpc_method` is a method that can be chosen from this `enum`.

```c
/* rpc_call.h */
enum monero_rpc_method {
  6     GET_HEIGHT,
  7     GET_BALANCE,
  8     GET_TXID,
  9     GET_LIST,
 10     GET_SUBADDR,
 11     NEW_SUBADDR,
 12     MK_IADDR,
 13     MK_URI,
 14     SPLIT_IADDR,
 15     GET_VERSION,
 16     END_RPC_SIZE
 17 };
```

You can loop through every method to initialise the data structure.

```c
 /* initialise monero_wallet with NULL */
 for (int i = 0; i < END_RPC_SIZE; i++) {
     monero_wallet[i].monero_rpc_method = i;
                 ...
     monero_wallet[i].host = NULL;
     monero_wallet[i].port = NULL;
     monero_wallet[i].user = NULL;
     monero_wallet[i].pwd = NULL;
                 ...
     monero_wallet[i].reply = NULL;
 }
```


## Error handling / output

- Every error should be printed to stderr,
- User information should be printed to stdout or a named pipe

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

​    Use a unique identifier when using openlog:

```c
mnp.c:      openlog("mnp:", LOG_PID, LOG_USER);
rpc_call.c: openlog("mnp:rpc_call:", LOG_PID, LOG_USER);
```


## Style guide

- A line of source code should not exceed 120 characters,
- indent/tab should consists of 4 whitespaces.

Comments should be made in the following way:

```c
/*
 * My comment
 * returns x
 */

/* My comment */
```

​    Try to avoid `// My comment`.
