#Monero Named Pipes

Monero named pipes (mnp) is a payment processor, using named pipes, to monitor incoming payments on 
a shell like bash or zsh. mnp is developed with the *UNIX-philosophy* in mind and allows interaction through named pipes.


## Getting started

mnp will create a set of files and directories - allowing you to control and check for incoming payments with command line tools.
Default = /tmp/mywallet


```bash
/tmp/mywallet/

├── transfer
│   ├── subaddress1
│   └── subaddress2
├── payment
│   ├── paymentID1
│   └── paymentID2
├── setup
│   ├── transfer
│   └── payment
├── balance
└── bc_height
```

### Three tiny example

To get an idea on how mnp works please study those three examples.
For more details see "How to setup a payment".

#### 1. Read the blockchain height:

A new block is found every 2 minutes on avarage.

```bash
# wait 2 minutes for reply
$ cat /tmp/mywallet/bc_height
```

#### 2. Monitor the balance:

```sudo apt-get install inotify-tools```
inotifywait works passiv - the operating system takes care of the rest.

```bash
#!/bin/bash
while inotifywait -e modify /tmp/mywallet/balance; do
    cat /tmp/mywallet/balance >> logfile;
done
```

#### 3. Monitor /tmp/wallet:

This allows interaction with any scripting language (perl, python, ...)

```bash
$ inotifywait -m /tmp/mywallet/ -e close_write -r |
  while read dir action file; do
      python check_payment.py ${dir}/${file}
  done
```

## How to build mnp

'libcurl' is required to build mnp.

```bash
$ cd mnp
$ mkdir build
$ cmake ..
$ make
$ make install
```

### Verify

gpg_key : https://github.com/d4ndox/mnp/blob/master/doc/d4ndo%40proton.me.pub

*Please also help audit the source code.*


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

### Config file ~/.mnp.ini:

The config file makes things easier. It is used by both `mnp` and `mnp-payment`.

```bash
; Monero named pipes (mnp)
; Configuration file for mnp.

[rpc]                           ;rpc host configuration
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

## How to setup a payment

Three ways to monitor a payment. You start by listing all subaddresses and decide which address
to use (index 0 is NOT recommend).

List all subaddresses and indices with the command mnp-payment:

```bash
$ ./mnp-payment --list
0 "A13iyF9bN7ReDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhSwjahXd"
1 "Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB"
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

