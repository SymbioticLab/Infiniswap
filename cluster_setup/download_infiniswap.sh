#!/bin/bash

set -e
set -x

dir="config"

for line in `cat ${dir}/daemon.list`
do
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    ./connect.exp ${host} ${ip} IN # download/update git directory
done
for line in `cat ${dir}/dashboard-server.list`
do
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    ./connect.exp ${host} ${ip} SER # setup server environment
done