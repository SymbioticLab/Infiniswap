#!/bin/bash

set -e
set -x

for line in `cat device.list`
do
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    ./connect.exp ${host} ${ip} IN # setup client
done