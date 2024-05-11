#!/bin/bash
inotifywait -m /tmp/mywallet -e create -r |
while read dir action file; do
    path=$dir$file;
    echo $path;
    amount=$(cat $path);
    echo "amount = "$amount;
done
