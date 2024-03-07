# Monero Named Pipes

Monero named pipes (mnp) is a leightweight payment processor, using named pipes, to monitor incoming payments on 
a shell like bash or zsh. mnp is developed with the *UNIX-philosophy* in mind and allows interaction through named pipes.

mnp will create a set of files and directories - allowing you to control and check for incoming payments with command line tools.
Default = /tmp/mywallet


```bash
/tmp/mywallet/

├── transfer
│   ├── subaddress1
│   └── subaddress2
└── payment
    ├── paymentID1
    └── paymentID2
```

### Example

For more details, see "How to set up a payment".
A small example to get an idea on how mnp works,

#### 1. Initalise mnp

```bash
# initialise the workdir
$ mnp --init
$ monero-wallet-rpc --tx-notify "/usr/bin/mnp %s"
```
#### 2. Monitor /tmp/mywallet/payment:

```sudo apt-get install inotify-tools```
inotifywait works passive - the operating system takes care of the rest.

    #!/bin/bash
    while inotifywait -e create /tmp/mywallet/payment -r | 
        while read dir action file; do
        echo "incoming txid:" $file;
    done

#### 3. Monitor /tmp/wallet with Python:

This allows interaction with any scripting language (Perl, Python, ...)

    #!/bin/bash
    while inotifywait -m /tmp/mywallet -e create -r |
    while read dir action file; do
        python check_payment.py ${dir}/${file}
    done

## How to build mnp

'libcurl' is required to build mnp. `apt-get install libcurl4`

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
 
- [ ] Step 1) Start monerod. It keeps the blockchain in sync.
- [ ] Step 2) Start monero-wallet-rpc --tx-notify "/usr/bin/mnp %s". It listens on rpc port and takes care of your wallet.

```bash
$ monerod --testnet
$ monero-wallet-rpc --tx-notify "/usr/bin/mnp %s" --testnet --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18083 --rpc-login username:password --wallet-file mywallet --prompt-for-password
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
verbose = 0                     ;verbose mode
account = 0                     ;choose account

[cfg]                           ;workdir configuration
workdir = /tmp/mywallet         ;wallet working directory
mode = rwx------                ;mode of workdir and pipes rwxrwxrwx
```

## How to set up a payment

Two ways to monitor a payment. You start by listing all subaddresses and decide which address
to use.
(index 0 = Primary Address).

```bash
$ ./mnp-payment --list
0 "A13iyF9bN7ReDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhSwjahXd"
1 "Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB"
```

### Without payment Id

A simple transfer. Typically used for donations. If you have a small business or only issue a few 
invoices a month, it is advised to create a sub-address for each invoice. This has privacy benefits.

```bash
$ tx=$(mnp-payment --subaddr 1)
$ echo $tx
Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB

# Create a QR code for the payment.
$ mnp-payment --subaddr 1 | qrencode -tUTF8
█████████████████████████████████████████████
████ ▄▄▄▄▄ █▀█ █▄█▄ ▄▀ █▄▀▄█▀ █ ▄█ ▄▄▄▄▄ ████
████ █   █ █▀▀▀█ ▀ █▄▀█▄█ ▀▄ ▀ ▄▀█ █   █ ████
████ █▄▄▄█ █▀ █▀▀█▄▀▄▀▀█▄█▀██▀▀ ▄█ █▄▄▄█ ████
████▄▄▄▄▄▄▄█▄▀ ▀▄█ █▄█ ▀▄▀▄▀ █▄█ █▄▄▄▄▄▄▄████
████  ▄▄ █▄   ▀▄▀▀▄  ▀██ ▀▀▀▄▄▄ ▀ ▀ █ █▄█████
████▀▀  ▀ ▄██ ▄█▀█ ▄██ ▀█ ▄▀▄█ ▄███   ▄▄ ████
█████▄█ █▀▄█▄▄▄█▄ ▄▄  ▄▀▀▀▄▄▄█ ▀  ▄▀▄█▄▀█████
████▄█▄▀▄ ▄▄█▀█ ▄▄ ▄▀█▄█▄▀█▀██▀▄▄▄▄█ ██ ▄████
████ █▄▄▀▀▄████▄▀▄▀▄▀▄▄ ▀▄ ▀█ ▄█▀   ▄███▄████
████▄▄ ▀▀█▄█ ███▀▄▀▄▄▄ ▀▀ ▀▀█▄▀ █▄ ▄▀█▄▄ ████
████▄█ █▀▀▄██▀▀█▄▄ ▄ ▀▀▀▀█▀██▄      ▄  ▀█████
██████▀ █▀▄▀▄▄█ ▄█  █▄ ██ ▀▀▀██▄ █ ▀ █▄▄ ████
████ ▄▀▀▀▀▄▄▄██▄▀▄▄▀ ▄█▄▀▄ ▀▄▄ ▄▀▄▄▀█▄█▄▄████
████ █▀ ▄█▄  █▀█▀▄█  ▄██▄ ▀█▀▄ ▄ ▀▀ █▀█▄▄████
████▄█▄█▄▄▄▄ █ █▄▀▄▀▀ ██▀▄ ▀▀█   ▄▄▄ ▀ █ ████
████ ▄▄▄▄▄ █▄▄▀ ▄█▄▄▄▄ █▀ █▀█▄▀▄ █▄█  █▄ ████
████ █   █ █ ██▄ █▄   ▀▀▀▄ ▀█▀▄▄     █▄█▄████
████ █▄▄▄█ █ ▄ ▀ ▄█ ▄███▄ ▀████ ▀▀█▄█▄█▄ ████
████▄▄▄▄▄▄▄█▄▄█▄▄██▄███▄▄███▄▄▄█████▄▄█▄▄████
█████████████████████████████████████████████

while [ ! –e ${tx} ]
do
  sleep 1
done

donation=$(cat /tmp/mywallet/transfer/$tx)
echo "Thank you for donating" $donation "piconero"

Thank you for donating 25000 piconero
```

### Include a payment Id

A retailer would use this to be able to match their orders to the payment.
Something like an invoice number.

```bash
# Create a random paymentid (16 hex character)
$ mypaymentId=$(openssl rand -hex 8)
$ echo $mypaymentId
e02c381aa2227436

# Create an integrated address. No need to spezify a subaddress -
# Priamary 0 is default by Monero spezification.
$ echo $mypaymentId | mnp-payment
AAkPz3y5yNweDPWW7FZoqd1 ... 5kqxkfnou78gMMeg

# Create a QR code for the payment.
$ mnp-payment $mypaymentId | qrencode -tUTF8
█████████████████████████████████████████████████
████ ▄▄▄▄▄ █▀█▄ ▀ ▀ ▄ ▄▄▄▄▀▄█▀▀▀▄ ▀█▄█ ▄▄▄▄▄ ████
████ █   █ █▀▀▀▄▀▄▀█▀▄▄▄▀ ▄█▀▀▀▀▄██▄▄█ █   █ ████
████ █▄▄▄█ █▀█ ▀▄ █ ▀▀▄█▀ ▀▄█▄█▀   █▄█ █▄▄▄█ ████
████▄▄▄▄▄▄▄█▄█▄▀ █▄▀▄█▄█▄▀ █▄█▄█▄█ █ █▄▄▄▄▄▄▄████
████▄   ▄█▄  ▄█ ▀▄▀  ▄▀█ ▄▄██ ▀█▀▄▀█▀▄▀ ▀▄▀▄█████
████ ▀▄█  ▄▄ ██▄▄█▀▄██▄█ ▀██▄▄▄ ██▄▀ ██▀█ ▄█▀████
████▄▄▀▄█▄▄█▀▀▀█▄█▀▀▀▀▄█▀ █▀▄▄▄█▀ ▀█ ▀█▄▀▄▀▀▀████
████ ▄▄▀ █▄██ ▀ ▄▄██▀██ ▀▀ ██▄█ █▄████▀██▀███████
██████▀▀▄▀▄█ ▄▀▄▄▀ ▀▀▀▀▄▀▄  ▀▀ █▀ █ ▄▀▀▀ ▄█▄▀████
████▄ ▄▀▄▄▄ ▄█ ██ ▄   ▀███▄▀ ▄ ▄▀█ ▀ ▄█▄▄▀█ █████
████▄█▄▄▀█▄▄█▄ █▀█▄ ▄█▀█▄ ▄█▄▄ ▄ ▄▀▀▀█▀▀ ▄ ▀▄████
████▄   ▀ ▄██▀██▀▄█ ▄███▄ ████  ▄█▄▀███▄ ▀███████
████▀▄█ ▀▄▄▄▀  ▀▄▄▄▄▀▄▄▀ ▀▄▄█▄▄▄ ▄▀█ ▀█ ▀▄  █████
████▄▀▄ ▄█▄▀  ████▀  █▄▀  ▀▀ █ ▄ ▄▀▀ ▄▀  █▄▀█████
████ ▄ ▄ █▄█ █▀█▄▀▀▀▀█▄█▀▄▀█▄▄▀▄ ███ ▄▀   ▄▀█████
████ █▀▀▄▄▄▀█▀▀ ▄▄▄█▀▄▄ ▀ ▀▀█▄█▄▄█▀██▀█▄█ ▄█▀████
████▄██▄▄▄▄▄ ██ █ ▄▀▀▀▀▄ █▀█▄█▄█ ▄▄  ▄▄▄ █▄  ████
████ ▄▄▄▄▄ █▄████ ▄ █ ▀█▄█▄██▄ ▄▀█▀▀ █▄█  ▄▀█████
████ █   █ █  ▄▄▀▀▄▀▄█▀█▄ ▄ ▄▄▀█▀▄▄█▄▄▄ ▄ ▄  ████
████ █▄▄▄█ █ █▄█▀▄▀ ▄▄▄█▄ ▀█ █▀  █▄▀█▀▀▀ ▀█▀█████
████▄▄▄▄▄▄▄█▄█▄█▄█▄▄█▄████▄▄▄█▄▄███▄█▄████▄██████
█████████████████████████████████████████████████

# Read amount received. BLOCKED read until transfer is done.
$ payment=$(cat /tmp/mywallet/payment/"$mypaymentId")
$ echo "Thank you for your payment" $payment "piconero"
Thank you for your payment 25000 piconero
```

## Information

You might consider to change the default color for 'ls' by adding this to your bashrc:
```bash
 echo 'LS_COLORS=$LS_COLORS:"pi=00;35"' >> ~/.bashrc
```
The default color of named pipes set by Ubuntu is a pain:

Licence: »GPLv3«

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

