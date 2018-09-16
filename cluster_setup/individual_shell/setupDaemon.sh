#!/bin/bash
# usage: ~ [ip] [port] [dir] [mode]
set -e
set -x
cd $3/infiniswap
git checkout GUI
cd setup
./ib_setup.sh $1
sleep 10
cd ../infiniswap_daemon
if [ $4 == "GUI" ]; then
make clean && make gui && ./infiniswap-daemon $1 $2
else
make clean && make && ./infiniswap-daemon $1 $2
fi