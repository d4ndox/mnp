# Monero Named Pipes

Monero named pipes (mnp) is a lightweight wallet designed to monitor incoming payments within a Unix shell environment. It uses named pipes for interaction, creating a set of files and directories within a specified working directory. Default: */tmp/mywallet/*.


## Directory Structure

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


## How to build mnp?

libcurl is required. Install it with:
```bash
apt-get install libcurl4
```

Build and install mnp:
```bash
cd mnp
mkdir build
cmake -DCMAKE_BUILD_TYPE=Release ../
make
sudo make install
```

gpg_key : https://github.com/d4ndox/mnp/blob/master/doc/d4ndo%40proton.me.pub


## How to Run mnp?

For details see the wiki [Getting Started](https://github.com/d4ndox/mnp/wiki/Getting-started).

1. Initialise mnp:
```bash
mnp --init
```

2. Start `monerod`:
```bash
monerod --detach
```

3. Start `monero-wallet-rpc` with configuration:
```bash
monero-wallet-rpc --config-file notify-mnp.cfg
```

`notify-mnp.cfg` example:
```cfg
rpc-bind-ip=127.0.0.1
rpc-bind-port=18083
rpc-login=username:password
wallet-file=mywallet
password=mywalletpassword
tx-notify=/usr/local/bin/mnp --confirmation 1 %s
```

4. [Optional] Start the Monero Named Pipe Daemon to monitor blockchain height and total balance:
```bash
mnpd --verbose
```


## How to Set Up a Payment?

For details see the wiki [Setup a Payment](https://github.com/d4ndox/mnp/wiki/Setup-a-payment).

Create a new subaddress:
```bash
mnp-payment --newaddr --amount 650000
```


## How to Monitor /tmp/wallet/transactions?

For details see the wiki [Monitor a Payment](https://github.com/d4ndox/mnp/wiki/Monitor-a-payment).

Read the pipes:
```bash
find /tmp/mywallet/transactions -type p -exec cat {} \;
```


## Close mnp [Optional]

Remove the work directory:
```bash
mnp --cleanup
```


## Additional Information

- Consider changing the default color for ls in your ~/.bashrc:
```bash
 echo 'LS_COLORS=$LS_COLORS:"pi=00;35"' >> ~/.bashrc
```

- Licence: MIT

- Author: d4ndo

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
