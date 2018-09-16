#!/bin/bash

set -e
set -x

dir="config"

for line in `cat ${dir}/device.list`
do
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    ./connect.exp ${host} ${ip} IN # setup client
done