#!/bin/bash

if [ ! -n "$1" ] ;then
MLNX_OFED_VERSION="4.*"
else
MLNX_OFED_VERSION=$1
fi
MOD_SYM=/var/lib/dkms/mlnx-ofed-kernel/${MLNX_OFED_VERSION}/build/Module.symvers
cp $MOD_SYM ../infiniswap_bd

