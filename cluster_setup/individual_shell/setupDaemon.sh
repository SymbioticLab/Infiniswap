#!/bin/bash
# usage: ~ [ip] [port] [dir] [mode]
set -e
set -x
cd $3/infiniswap
cd setup
./ib_setup.sh $1
sleep 10

if [ $4 == "GUI" ]; then
    sed -i 's/user_need_gui=0/user_need_gui=1/g' install.sh
else
    sed -i 's/user_need_gui=1/user_need_gui=0/g' install.sh
fi
./install.sh daemon

cd ../infiniswap_daemon
# if [ $4 == "GUI" ]; then
# make clean && make gui && ./infiniswap-daemon $1 $2
# else
# make clean && make && ./infiniswap-daemon $1 $2
# fi

./infiniswap-daemon $1 $2