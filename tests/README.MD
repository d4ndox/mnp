# Testing Monero Named Pipe.

- Q: Why to use a database for testing?

- A: To avoid problems with concurrency that can arise when several transactions run simultaneously.

  

  Create a new database and user:

```bash
sudo mysql
```

```sql
CREATE USER 'sysadmin'@'localhost' IDENTIFIED WITH authentication_plugin BY 'mypassword';
```

```sql
GRANT PRIVILEGE ON payDB.payments TO 'sysadmin'@'localhost';
```

```sql
GRANT CREATE, ALTER, DROP, INSERT, UPDATE, DELETE, SELECT, REFERENCES, RELOAD on *.* \
TO 'sysadmin'@'localhost' WITH GRANT OPTION;
```

```sql
FLUSH PRIVILEGES;
```

```bash
mysql --user=sysadmin --password=mypassword < create_db.sql
mysql --user=sysadmin --password=mypassword -e "SELECT * FROM payDB.payments;"
```

```sql
+-------+--------+-----------+
| PAYID | AMOUNT | STATUS    |
+-------+--------+-----------+
|     1 | 500000 | REQUEST   |
|     2 | 787878 | WAITING   |
|     3 | 787878 | TIMEOUT   |
|     4 | 123456 | FAILED    |
|     5 | 523456 | COMPLETED |
+-------+--------+-----------+
```

​	Start txdetect:

```bash
./txdetectdb.sh
```

​	Create a new payment request:

```bash
$ ./create_uri 55555
monero:AAkPz3y5yNweDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhfJb3BrWxFGT1maXEsT?tx_amount=0.000000055555

$ mysql --user=sysadmin --password=mypassword -e "SELECT * FROM payDB.payments;" | tail -n1
6	55555	REQUEST
```

The payment status will be set by the ```txdetectdb.sh``` exception is REQUEST - set by ```create_uri.sh```.

```sql
+-----------+
| STATUS    |
+-----------+
| REQUEST   |  set by create_uri. Request a new payment with paymentid set from PAYID.
| WAITING   |  set by txdetect as soon paymentid is detexted on /tmp/mywallet.
| TIMEOUT   |  set by txdetect when txdetect runs into timeout. atm 60 min.
| FAILED    |  set by txdetect when received amount is not equal to AMOUNT in table.
| COMPLETED |  set by txdetect when received amount is equal to AMOUNT in table.
+-----------+
```

# Test Checklist

## mnp

### Basic:
- [ ] mnp version
- [ ] mnp --version
- [ ] mnp help
- [ ] mnp --help
- [ ] mnp init
- [ ] mnp cleanup help
- [ ] mnp cleanup
- [ ] mnp balance help
- [ ] mnp balance
- [ ] mnp bc-height

### Payment:
- [ ] mnp payment help
- [ ] mnp payment new
- [ ] mnp payment list
- [ ] mnp payment subaddr 0
- [ ] mnp payment subaddr 1 --amount 9999999
- [ ] mnp payment e02c381aa2227436
- [ ] echo e02c381aa2227436 | mnp payment

### Monitor: 
- [ ] mnp "$TXID"
- [ ] mnp "$TXID" --notify-at 1
- [ ] mnp "$TXID" --notify-at 2 --confirmation 1
- [ ] mnp "$TXID" --notify-at 3
- [ ] echo "$TXID" | ./mnp
- [ ] echo "$TXID" | ./mnp --notify-at 2 --confirmation 1

### Proofs:
- [ ] mnp tx_proof help
- [ ] mnp balance
- [ ] mnp spend-proof "$TXID" --signature "$SIGNATURE"
- [ ] echo "$TXID" | mnp spend-proof --signature "$SIGNATURE"
- [ ] mnp tx-proof "$TXID" --address "$ADDRESS" --signature "$SIGNATURE"
- [ ] echo "$TXID" | mnp tx-proof --address "$ADDRESS" --signature "$SIGNATURE"

### Errors:
- [ ] mnp invalid-command
- [ ] mnp balance foo
- [ ] mnp bc-height foo
- [ ] mnp payment foo
- [ ] mnp payment subaddr -1
- [ ] mnp spend-proof
- [ ] mnp tx-proof
- [ ] mnp --version foo
- [ ] mnp --help foo

### stdout/stderr Exitcode:
- [ ] mnp balance
      echo $?
- [ ] mnp bc-height
      echo $?
- [ ] mnp spend-proof "$TXID" --signature "$SIGNATURE"
      echo $?
- [ ] balance="$(./mnp balance)"
      printf 'balance=%s\n' "$balance"
- [ ] height="$(./mnp bc-height)"
      printf 'height=%s\n' "$height"

## release

- [ ] increase VERSION in globaldefs.h


