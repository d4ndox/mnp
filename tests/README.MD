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

