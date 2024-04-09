#!/bin/bash

if [[ $# -eq 0 ]] ; then
    echo 'Amount missing'
    exit 1
fi

amount=$1

mysql --batch \
 --user=sysadmin \
 --password=mypassword \
 -e "INSERT INTO payDB.payments (AMOUNT, STATUS) \
    VALUES ('$amount','REQUEST');SELECT LAST_INSERT_ID();" | 
    tail -n1 | 
    xargs printf "%016x\n" | 
    mnp-payment --amount $amount
