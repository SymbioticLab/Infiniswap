#!/bin/bash
# usage: /setupGUI.sh [mode] [ip] [hostport] [clientport] [dir] [server_public_ip]
set -e 
set -x
cd $5/infiniswap
git checkout GUI
cd infiniswap_gui
make
# setup master
if  [ $1 == "server" ]; then
    node main.js &
    sudo ./runserver $3 $4 $6
# setup worker
elif [ $1 == "client" ]; then
    sudo ./runworker $2 $3 $4 $6
fi