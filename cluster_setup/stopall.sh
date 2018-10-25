#!/bin/bash

# run this script on the host machine and it will automatically kill all the running infiniswap and GUI programs

set -e
set -x

dir="config"

for line in `cat ${dir}/daemon.list`
do  
    echo $line
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    ./connect.exp ${host} ${ip} ST
done 
tmux kill-server