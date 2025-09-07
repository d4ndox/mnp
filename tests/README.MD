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

- [ ] mnp --help
- [ ] mnp -h
- [ ] mnp --?
- [ ] mnp --version
- [ ] mnp -v
- [ ] mnp --workdir /tmp/x --init
- [ ] mnp --workdir /tmp/x --cleanup
- [ ] mnp -w /tmp/x --init
- [ ] mnp -w /tmp/x --cleanup
- [ ] mnp --rpc_user none
- [ ] mnp --rpc_password none
- [ ] mnp --rpc_host 10.0.0.1
- [ ] mnp --rpc_port 20000
- [ ] mnp --rpc_host 10.0.0.1 --rpc-port 20000
- [ ] test --spend-proof
```bash
      mnp --spend-proof --signature $(cat monero_spend_proof_message) --message "test" 563bd8ddd9a03adec948673aee6d8ed87b400662d8dab81d19404c6f59da4fa3
      echo 563bd8ddd9a03adec948673aee6d8ed87b400662d8dab81d19404c6f59da4fa3 | mnp --spend-proof --signature $(cat monero_spend_proof_message) --message "test"
```
- [ ] test --spend-proof no message
```bash
      mnp --spend-proof --signature $(cat monero_spend_proof) 563bd8ddd9a03adec948673aee6d8ed87b400662d8dab81d19404c6f59da4fa3
      echo 563bd8ddd9a03adec948673aee6d8ed87b400662d8dab81d19404c6f59da4fa3 | mnp --spend-proof --signature $(cat monero_spend_proof)
```
- [ ] test --tx-proof no message long option
```bash
      mnp --tx-proof --address Bcwr43fNrww2WqcbB58juxbJCra47TyMaEsteVXTsYWCZLgzPTx4nVqgvj9PznzETxZ7TNxNyRbuVh3wowv58gz4BM3CCEg --signature $(cat monero_tx_proof) c6b256333295de891ac9768e637706f4c345775d543cff2e7ccc900b3929f214
      echo 11ed34037e69d7f3b17803c836b467f439f02463d47612a8685d3bfd0efbc6e9 | mnp --spend-proof -s $(cat spend_proof)

echo c6b256333295de891ac9768e637706f4c345775d543cff2e7ccc900b3929f214 | mnp --tx-proof --address Bcwr43fNrww2WqcbB58juxbJCra47TyMaEsteVXTsYWCZLgzPTx4nVqgvj9PznzETxZ7TNxNyRbuVh3wowv58gz4BM3CCEg --signature $(cat monero_tx_proof)
```
- [ ] test --tx-proof + message long option
```bash
      mnp --tx-proof --address Bcwr43fNrww2WqcbB58juxbJCra47TyMaEsteVXTsYWCZLgzPTx4nVqgvj9PznzETxZ7TNxNyRbuVh3wowv58gz4BM3CCEg --signature $(cat monero_tx_proof) c6b256333295de891ac9768e637706f4c345775d543cff2e7ccc900b3929f214
      echo 11ed34037e69d7f3b17803c836b467f439f02463d47612a8685d3bfd0efbc6e9 | mnp --spend-proof -s $(cat spend_proof)

echo c6b256333295de891ac9768e637706f4c345775d543cff2e7ccc900b3929f214 | mnp --tx-proof --address Bcwr43fNrww2WqcbB58juxbJCra47TyMaEsteVXTsYWCZLgzPTx4nVqgvj9PznzETxZ7TNxNyRbuVh3wowv58gz4BM3CCEg --signature $(cat monero_tx_proof)
```

## mnpd

- [ ] mnpd --help
- [ ] mnpd -h
- [ ] mnpd --version
- [ ] mnpd -v
- [ ] mnpd --workdir /tmp/nonexisting
- [ ] mnpd -w /tmp/nonexisting
- [ ] mnpd --rpc_user notexisting
- [ ] mnpd --rpc_password wrongpassword
- [ ] mnpd --rpc_host 10.0.0.1
- [ ] mnpd --rpc_host 127.0.0.1
- [ ] mnpd --rpc_port 20000
- [ ] mnpd --rpc_port 18083
- [ ] mnpd --rpc_host 10.0.0.1 --rpc_port 20000
- [ ] mnpd --rpc_host 127.0.0.1 --rpc_port 18083

## mnp-payment

- [ ] mnp-payment --help
- [ ] mnp-payment -h
- [ ] mnp-payment --version
- [ ] mnp-payment -v
- [ ] mnp-payment --list
- [ ] mnp-payment -l
- [ ] mnp-payment --rpc_host 10.0.0.1 --list
- [ ] mnp-payment --rpc_host 127.0.0.1 --list
- [ ] mnp-payment --rpc_port 20000 --list
- [ ] mnp-payment --rpc_port 18083 --list
- [ ] mnp-payment --rpc_host 127.0.0.1 --rpc_port 18083 --list
- [ ] mnp-payment --subaddr 0
- [ ] mnp-payment -s 0
- [ ] mnp-payment -s 1 --amount 999999
- [ ] mnp-payment -subaddr 2 -x 999999
- [ ] mnp-payment --newaddr
- [ ] mnp-payment -n
- [ ] mnp-payment --newaddr --amount 999999
- [ ] echo 0000000000000001 | mnp-payment
- [ ] mnp-payment 0000000000000001
- [ ] echo 0000000000000002 | mnp-payment --amount 2222222
- [ ] mnp-payment --amount 2222222 0000000000000002
- [ ] echo 0000000000000003 | mnp-payment -x 3333333
- [ ] mnp-payment -x 3333333 0000000000000003

## release

- [ ] increase VERSION in globaldefs.h


