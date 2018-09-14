#!/bin/bash
# usage: ~ [dir] (if no dir specified, infiniswap is ../ directory)
# used after ib_setup has completed

set -x

if [ $# -gt 0 ]; then
    cd $1/infiniswap
else
    cd ../
fi

git checkout Dashboard
cd infiniswap_bd
make clean
./autogen.sh
./configure
make
make install
swapoff /dev/sda3
cd ../setup
./infiniswap_bd_setup.sh