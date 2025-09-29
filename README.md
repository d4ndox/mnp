# Monero Named Pipes

Monero named pipes (mnp) is a lightweight wallet designed to monitor incoming payments within a Unix shell environment. It uses named pipes for interaction, creating a set of files and directories within a specified working directory. Default: */tmp/mywallet/*.


## Directory Structure

```bash
$ tree
.
├── balance
├── bc_height
├── double_spend_alert
├── transactions
│   ├── 64753821918b2f856815ae2894240301e41c3ae799b4e6f2af96604dda1cc50b
│   │   └── 778we6Tb3c7c4VBEZ839sy98GFMQzGVtE5qMD8NzP1QvCxysFCYhv65NJ5Jiun8ssJ31hX8uSP3rMV6nJEXvcsx4JGNEb3U
│   ├── b70d4b78beeea0e10f12b59c3f0bdedb004c7f7dc19c6461a553e8a272659eeb
│   │   ├── 7AXroCmKa1nXRo3Rq371sqhdDQfm8uPNF8bDx3LGSMJHZzwiKxmu3PAR9pfX8b8KFC812JuH9ZHPK1fapwTA1QT4TanWEfy
│   │   └── 7BD4hHAJKX91pPUzbiRKm5VMYXh8pCArAXfwGsUQ9gfaaMsTiwxuA6jaGzR9brDPvBQkHh6anvZfiRu4CHAyLuDtFuBrpzz
│   └── d38b97997687a471c696ed72502a7cf2ee6e8b1d39b4c6a6609b364c8101b1d4
│       └── 00000000c000001a
└── txid

4 directories, 8 files
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

gpg_key : [d4ndo@proton.me](https://github.com/d4ndox/mnp/blob/master/doc/d4ndo%40proton.me.pub).


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
