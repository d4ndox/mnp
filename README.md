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

A short example in four simple steps to give you an idea of how mnp works.
For more details, see "How to set up a payment".

#### 1. Initalise mnp

```bash
# initialise the workdir
$ mnp --init
$ monero-wallet-rpc --tx-notify "/usr/local/bin/mnp --confirmation 3 %s"
```

#### 2. Monitor /tmp/mywallet/

To monitor /tmp/mywallet i recommend inotifywait. It efficiently waits for changes to files using Linux's inotify.
The operating system takes care of the rest.
```sudo apt-get install inotify-tools```

```bash
#!/bin/bash
inotifywait -m /tmp/mywallet -e create -r |
while read dir action file; do
    echo $file;
    amount=$(cat ${dir}${file});
    echo "Received :" $amount;
done
```
Be patient and wait for 3 confirmations.

#### 3. Setup a payment

The amount is specified in the smallest unit piconero.

```bash
$ mnp-payment --amount 500000000000 --newaddr
monero:BbE3cKKZp7repvTCHknzg4TihjuMmjNy78VSofgnk28r26WczcZvPcufchGqqML7yKEYZY91tytH47eCSA6fCJRRNy7cqSM?tx_amount=0.500000000000
```

Pipe the output to ```qrencode``` to create a QR-Code for your customer. 

#### 4. Close mnp

Close the working directory /tmp/myallet/. If you stopped all monitoring you might want to close the workdir. (optional)

```bash
# remove the workdir /tmp/mywallet
$ mnp --cleanup
```


## Command line option:

mnp is called from monero-wallet-rpc --tx-notify or some other source.

```bash
Usage: mnp [OPTION] [TXID]

   [TX_ID]
	   TXID is the transaction identifier passed from
	   monero-wallet-rpc --tx-notify or some other source.

  -a  --account [ACCOUNT]
           Monero account number. 0 = default.

  --confirmation [n]
           amount of blocks needed to confirm transaction.

  --init
           create workdir for usage.

  --cleanup
           delete workdir.
```

mnp-payment is used to prepare a payment.

```bash
Usage: mnp-payment [OPTION] [PAYMENT_ID]

  [PAYMENT_ID]
		PAYMENT_ID is a 16 hex unique char to
		identify the payment. Returns an
		integrated address.

  -a  --account [ACCOUNT]
               Monero account number. 0 = default.

  -l, --list
               list all subaddresses + address_indices.

  -s  --subaddr [INDEX]
               returns subaddress on INDEX.

  -n  --newaddr
               returns a new created subaddress.

  -x  --amount [AMOUNT]
               AMOUNT is specified in piconero.
               returns a URI string.
```


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
$ monerod
$ monero-wallet-rpc --tx-notify "/usr/local/bin/mnp --confirmation 3 %s" --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18083 --rpc-login username:password --wallet-file mywallet --prompt-for-password
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

Two ways to monitor a payment. 

### 1. Without a payment Id

A simple transfer - typically used for donations. If you have a small business or only issue a few
invoices a day, it is advised to create a subaddress for each invoice. This has privacy benefits.

#### Create a new subaddress

```bash
$ mnp-payment --newaddr
Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB
```

#### List all subaddress

```bash
# (index 0 = Primary Address).
$ ./mnp-payment --list
0 "A13iyF9bN7ReDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhSwjahXd"
1 "Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB"
```

#### Select a subaddress

```bash
$ mnp-payment --subaddr 1
Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB
```

### 2. Including a payment Id

A retailer would use this to be able to match their orders to the payment.
Something like an invoice number. You pass a 16 hex character via pipe or command line option.
Primary Address 0 is allways used to create an integrated address. A privacy weakness.
(integrated address = primary address + payment Id).

#### Payment Id

The paymentId is a 16 hex character. Could be a checksum of your invoice number or just some random 
number. You could also increase the number by one vor every purchase.

```bash
# Create a random paymentid (16 hex character)
$ mypaymentId=$(openssl rand -hex 8)
$ echo $mypaymentId
e02c381aa2227436
```

#### Create an integrated address

```bash
# Create an integrated address.
$ echo $mypaymentId | mnp-payment
AAkPz3y5yNweDPWW7FZoqd1 ... 5kqxkfnou78gMMeg
```

## How to Monitor /tmp/wallet

### Using Python:

This allows interaction with any scripting language (Perl, Python, ...)

```bash
#!/bin/bash
while inotifywait -m /tmp/mywallet -e create -r |
while read dir action file; do
    python check_payment.py ${dir}/${file}
done
```

```bash
while [ ! –e ${tx} ]
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

