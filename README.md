# Monero Named Pipes

Monero named pipes (mnp) is a lightweight wallet designed to monitor incoming payments within a Unix shell environment. It uses named pipes for interaction, creating a set of files and directories within a specified working directory. Default: */tmp/mywallet/*.


```bash
/tmp/mywallet/

transactions
 ├── txid_1
 │   └── "subaddress_1"
 ├── txid_2
 │   └── "subaddress_2"
 └── txid_3
     └── "paymentId_1"
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


## How to run mnp?

 Monero Named Pipes uses monero-wallet-rpc which comes with the Monero Command-line Tools. Download @ getmonero.org.

- [ ] Step 1) Start monerod. It keeps the blockchain in sync.
- [ ] Step 2) Start monero-wallet-rpc --tx-notify "/usr/bin/mnp %s". It listens on rpc port and takes care of your wallet.

```bash
$ mnp --init
$ monerod
$ monero-wallet-rpc --tx-notify "/usr/local/bin/mnp --confirmation 1 %s" --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18083 --rpc-login username:password --wallet-file mywallet --prompt-for-password
$ mnp-payment --list
```

If 'mnp-payment --list' fails, adapt rpc configuration in '~/.mnp.ini'


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
mode = rwx------                ;permission of workdir rwxrwxrwx
pipe = rw-------                ;permission of pipes rwxrwxrwx
```


## Command line option:

mnp is called from monero-wallet-rpc --tx-notify or some other source.

```bash
Usage: mnp [OPTION] [TXID]

   [TX_ID]
	   TXID is the transaction identifier passed from
	   monero-wallet-rpc --tx-notify or some other source.

  --account [ACCOUNT]
           Monero account number. 0 = default.

  --notify-at [0,1,2,3] default = confirmed
           0, none
           1, txpool
           2, confirmed
           3, unlocked

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


## Setting up a payment:

To set up a payment using mnp, you have several options based on your specific requirements:

### 1. Simple Transfer (No Payment ID):

For simple transfers, such as donations or general payments, you can create a new subaddress and provide it to the payer.

#### **Create a New Subaddress:**
```bash
$ mnp-payment --newaddr
Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB
```
This command generates a new Monero subaddress, providing enhanced privacy and security for receiving payments.

#### **List All Subaddresses:**
To list all existing subaddresses along with their indices, you can use:
```bash
$ mnp-payment --list
0 "A13iyF9bN7ReDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhSwjahXd"
1 "Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB"
```
This command lists all subaddresses associated with your Monero wallet, including the primary address (0).

#### **Select a Specific Subaddress:**
If you prefer to specify a particular subaddress, you can do so by providing its index:
```bash
$ mnp-payment --subaddr 1
Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB
```
This command selects the subaddress at index 1 for receiving payments.

#### **Add an Amount:**
After selecting a subaddress, you can specify the payment amount in piconero (the smallest unit of Monero):
```bash
$ mnp-payment --subaddr 1 --amount 650000
monero:Bdxcxb5WkE84HuNyzoZvTPincGgPkZFXKfeQkpwSHew1cWwNcBXN4bY9YXY9dAHfibRBCrX92JwzmASMXsfrRnQqMo3ubLB?tx_amount=0.000000650000
```
This command prepares a payment with the specified amount and returns a Monero URI string, which can be provided to the payer.

### 2. Including a Payment ID (For Matching Orders):

In scenarios where matching orders to payments is essential, such as retail transactions or invoicing, you can generate an integrated address with a payment ID.

#### **Generate a Payment ID:**
Generate a unique 16-hex character payment ID using tools like OpenSSL:
```bash
# Create a random paymentid (16 hex character)
$ mypaymentId=$(openssl rand -hex 8)
$ echo $mypaymentId
e02c381aa2227436
```
This command generates a random 16-hex character payment ID.

#### **Create an Integrated Address:**
Use the generated payment ID to create an integrated address, combining it with your primary address:
```bash
# Create an integrated address.
$ echo $mypaymentId | mnp-payment
AAkPz3y5yNweDPWW7FZoqd1 ... 5kqxkfnou78gMMeg
```
This command generates an integrated address with the provided payment ID, enabling efficient tracking of payments.

#### **Add an Amount (Optional):**
If a specific amount needs to be included in the payment, you can do so using the **```--amount```** option:
```bash
 $ echo $mypayment | mnp-payment --amount 50000
monero:AAkPz3y5yNweDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhfT5kqxkfnou78gMMeg?tx_amount=0.000000050000
```
This command sets the payment amount and returns a Monero URI string for inclusion in invoices or payment requests.

### 3. Create a QR-Code (For Customer Convenience):
To facilitate easy payment by customers using mobile Monero wallets, you can generate a QR code from the payment URI.

#### **Install QR Code Generator:**
Ensure you have a QR code generator installed on your system. You can install qrencode using:
```bash
sudo apt-get install qrencode
```
#### **Generate QR Code:**
Use qrencode to generate a QR code from the Monero URI string:
```bash
mnp-payment --subaddr 1 | tr -d '\n' | qrencode -t UTF8
```
Monerujo has some problems with \n terminated strings, so it is better to remove them.

These methods provide flexible options for setting up payments using mnp. Every output of ```mnp-payment``` can be used to create an QR-Code. 


## How to Monitor /tmp/wallet

### Using a bash script:

```bash
#!/bin/bash

WATCHDIR="/tmp/mywallet/transactions"

# Function to handle reading from a pipe
read_pipe() {
    local dir="$1"
    local file="$2"
    local amount

    # Set timeout for cat command
    if ! amount=$(timeout 125m cat "${dir}/${file}"); then
        # Handle timeout - write to syslog and exit
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"
        exit 1
    fi

    echo "Received from $file: $amount"
}

# Trap SIGINT to clean up child processes
trap 'kill $(jobs -p); exit' SIGINT

# 1. At start all existing pipes need to be read.
find "$WATCHDIR" -type p 2>/dev/null | while read fifo; do
    dir=$(dirname "$fifo")
    file=$(basename "$fifo")
    (
        read_pipe "$dir" "$file"
    ) &
done

# Watch for new directories using inotifywait
inotifywait -m "$WATCHDIR" -e create --format '%w%f' |
while read new_path; do
    if [ -d "$new_path" ]; then
        echo "New txId detected: $new_path"

        # Lesen der einzigen Datei im Verzeichnis
        pipe_file=$(ls "$new_path")
        if [ -p "$new_path/$pipe_file" ]; then
            echo "New fifo pipe detected: $pipe_file in $new_path"
            read_pipe "$new_path" "$pipe_file" &
        else
            echo "No pipe found in $new_path"
        fi
    fi
done
```

See example for more.

## Close mnp

Close the working directory /tmp/myallet/. If you stopped all monitoring you might want to close the workdir. (optional)

```bash
# remove the workdir /tmp/mywallet
$ mnp --cleanup
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

