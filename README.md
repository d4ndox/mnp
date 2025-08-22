# Monero Named Pipes

Monero named pipes (mnp) is a lightweight wallet designed to monitor incoming payments within a Unix shell environment. It uses named pipes for interaction, creating a set of files and directories within a specified working directory. Default: */tmp/mywallet/*.


```bash
$ tree /tmp/mywallet/
/tmp/mywallet/
├── balance
├── bc_height
└── transactions
    ├── 96fcb1d41d8b90af1dcae770a2ef4bf84b984412fc2a4a6ea9a169dce43f8ee4
    │   └── BdQNo1EL1KVcnwQEiHWnDKJun4MshkC8s2RjbF1yoT9pKfc9zWtWwkf3NxuBDfKKhkAwBJK7UPeigKmVWVaXg5iPFqLqq6A
    ├── 9da47eec38e70001e251761dabcc69780ab96ca4c2ab7b83b67f86ca00dfeb7b
    │   └── 1be3bfb7413adda5
    └── c503489e9e4edb4451288fc6548b0d765ccd0890634d6e7e523ef0cdaf86cd1e
        └── BdQNo1EL1KVcnwQEiHWnDKJun4MshkC8s2RjbF1yoT9pKfc9zWtWwkf3NxuBDfKKhkAwBJK7UPeigKmVWVaXg5iPFqLqq6A

4 directories, 5 files
```

It enables users to control and track incoming payments through command-line tools.


## How to build mnp

'libcurl' is required to build mnp. `apt-get install libcurl4`

```bash
$ cd mnp
$ mkdir build
$ cmake -DCMAKE_BUILD_TYPE=Release ../
$ make
$ sudo make install
```

### Verify

gpg_key : https://github.com/d4ndox/mnp/blob/master/doc/d4ndo%40proton.me.pub

*Please also help audit the source code.*


## How to Run mnp?

For details see the wiki [Getting Started](https://github.com/d4ndox/mnp/wiki/Getting-started).

Create a work directory for your wallet (`/tmp/mywallet/` is default):
```bash
mnp --init
```
Start `monerod`
```bash
monerod --detach
```
Start `monero-wallet-rpc`
```bash
monero-wallet-rpc --config-file notify-mnp.cfg
```

_notify-mnp.cfg_
```cfg
rpc-bind-ip=127.0.0.1
rpc-bind-port=18083
rpc-login=username:password
wallet-file=mywallet
password=mywalletpassword
tx-notify=/usr/local/bin/mnp --confirmation 1 %s
```

[OPTIONAL] 

Start the Monero Named Pipe **Daemon** to monitor the _bcheight_ and total _balance_: `mnpd`


## How to set up a payment:

For details see the wiki [Setup a Payment](https://github.com/d4ndox/mnp/wiki/Setup-a-payment).

Create a New Subaddress:
```bash
mnp-payment --newaddr --amount 650000
```
`monero:Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB?tx_amount=0.000000650000`


## How to Monitor /tmp/wallet/transactions

For details see the wiki [Monitor a Payment](https://github.com/d4ndox/mnp/wiki/Monitor-a-payment).

Using cat to read the pipe:
```bash
find /tmp/mywallet/transactions -type p -exec cat {} \;
```

The pipe is closed as soon, it has been read.


## Close mnp [OPTIONAL]

Close the working directory /tmp/myallet/. If you stopped all monitoring you might want to close the workdir. (optional)

```bash
# remove the workdir /tmp/mywallet
mnp --cleanup
```

## Information

You might consider to change the default color for 'ls' by adding this to your bashrc:
```bash
 echo 'LS_COLORS=$LS_COLORS:"pi=00;35"' >> ~/.bashrc
```
The default color of named pipes set by Ubuntu is a pain:

Licence: »MIT«

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

