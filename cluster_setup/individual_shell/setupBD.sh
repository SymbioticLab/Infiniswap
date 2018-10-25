#!/bin/bash
# usage: ~ [dir] [mode] (if no dir specified, infiniswap is ../ directory)
# used after ib_setup has completed

set -x

if [ $# -gt 0 ]; then
    cd $1/infiniswap
else
    cd ../../
fi


cd setup
if [ $# -gt 1 ] && [ $2 == 'GUI' ]; then
    sed -i 's/user_need_gui=0/user_need_gui=1/g' install.sh
else
    sed -i 's/user_need_gui=1/user_need_gui=0/g' install.sh
fi


./install.sh bd

# cd infiniswap_bd
# # if [ $# -gt 1 ] && [ $2 == 'GUI' ]; then
# #     sed -i 's/\/\/#define IS_GUI/#define IS_GUI/g' infiniswap.h
# # else
# #     grep '//#define IS_GUI' infiniswap.h
# #     if [ $? == 1 ]; then
# #         sed -i 's/#define IS_GUI/\/\/#define IS_GUI/g' infiniswap.h
# #     fi
# # fi

# make clean
# ./autogen.sh
# ./configure
# make
# make install

swapoff /dev/sda3
cd ../setup
./infiniswap_bd_setup.sh