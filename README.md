#Monero Named Pipes

Monero named pipes (mnp) is a payment processor, using named pipes, to monitor incoming payments on bash.
mnp is developed with the *UNIX-philosophy* in mind and allows interaction through named pipes.


## Getting started

mnp will create a set of files and directories allowing you to control the client with command line tools.
default = /tmp/mywallet


```bash
/tmp/mywallet/

├── transfer
│   ├── subaddress1
│   └── subaddress2
├── payment
│   ├── paymentID1
│ 	└── paymentID2
├── setup
│   ├── transaction
│   └── payment
├── total-balance
├── total-unlocked-balance
├── bc_height
└── version
```

**Example:**
Monitor the blockchain height:

```bash
$ inotifywait /tmp/mywallet/bc_height
```

or read the total balance:

```bash
$ cat total-balance
0000000000000
```

This allows interaction with any scripting language (perl, python, php, bash, zsh, ...)


### Compile

```bash
$ cd mnp
$ mkdir build
$ cmake ..
$ make
$ make install
```

### Verify

My public gpg_key :

*Please audit the source code.*


## How to run mnp?

 Monero Named Pipes uses monero-wallet-rpc which comes with the Monero Command-line Tools. Download @ getmonero.org.

```bash

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
 
- [ ] Step 1) Start monerod. It keeps the blockchain in sync.
- [ ] Step 2) Start monero-wallet-rpc. It listens on rpc port and takes care of your wallet.
- [ ] Step 3) Start mnp.

```bash
$ monerod --testnet
$ monero-wallet-rpc --testnet --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18083 --rpc-login username:password --wallet-file mywallet --prompt-for-password
$ mnp --rpc_host 127.0.0.1 --rpc_port 18083 --rpc_password password --rpc_user username --workdir="/tmp/mywallet"
```

### Config

The config file ~/.mnp.ini:

```bash
; Monero named pipes (mnp)
; Configuration file for mnp.

[rpc]							;rpc host configuration
user = username                 ;rpc user
password = password             ;rpc password
host = 127.0.0.1                ;rpc ip address or domain
port = 18083                    ;rpc port 

[mnp]                           ;general mnp configuration
daemon = 0                      ;run mnp as daemon
verbose = 0                     ;verbose mode
account = 0                     ;choose account

[cfg]                           ;workdir configuration
workdir = /tmp/mywallet         ;wallet working directory
mode = rwx------                ;mode of workdir and pipes rwxrwxrwx
```

## Information

License: »GPLv3«

Author: »Unknown« (d4ndo)

```bash

     __
w  c(..)o     (
 \__(-)     __)
     /\    (
    /(_)___)
   w /|
    | \
   m  m | Monero Named Pipes.
```


mnp is a hobby project. Fun has the highest priority.

