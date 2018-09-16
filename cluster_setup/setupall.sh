#!/bin/bash

# run this script on the host machine and it will automatically setup infiniswap on all the devices in the list

set -e
set -x


daemon_port="9400";
dir="config"

if [ "$#" -eq 1 ] && [ "$1" == "GUI" ]; then
server_host=`cat ${dir}/server.list | cut -d : -f 1`
server_ip=`cat ${dir}/server.list | cut -d : -f 2`
tmux new -s server -d && tmux send -t server "./connect.exp ${server_host} ${server_ip}  GUI server ${server_host}" ENTER # setup GUI server
fi

sleep 5

for line in `cat ${dir}/device.list`
do
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`
    if [ "$#" -eq 1 ] && [ "$1" == "GUI" ]; then
    tmux new -s ${ip//./_}c -d && tmux send -t ${ip//./_}c "./connect.exp ${host} ${ip} GUI client ${server_host}" ENTER # setup GUI client
    tmux new -s ${ip//./_}d -d && tmux send -t ${ip//./_}d "./connect.exp ${host} ${ip} DM ${daemon_port} GUI" ENTER # setup daemon
    else
    tmux new -s ${ip//./_}d -d && tmux send -t ${ip//./_}d "./connect.exp ${host} ${ip} DM ${daemon_port} BASIC" ENTER # setup daemon
    fi
    
done

sleep 30

for line in `cat ${dir}/bd.list`
do  
    host=`echo $line | cut -d : -f 1`
    ip=`echo $line | cut -d : -f 2`

    tmux new -s ${ip//./_}b -d && tmux send -t ${ip//./_}b "./connect.exp ${host} ${ip} BD" ENTER # setup bd 
done 