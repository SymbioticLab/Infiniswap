#!/bin/bash
# usage: /setupGUI.sh [mode] [ip] [hostport] [clientport] [dir] [server_public_ip]
set -e 
set -x
cd $5/infiniswap/infiniswap_gui
git checkout linux
cd socket
sudo ./compile
# setup master
if  [ $1 == "server" ]; then
    sudo ./runserver $3 $4 $6
# setup worker
elif [ $1 == "client" ]; then
    sudo ./runworker $2 $3 $4 $6
fi