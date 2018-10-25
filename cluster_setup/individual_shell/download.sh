#!/bin/bash
# usage: ~ [dir]

set -x

cd $1
if [ ! -d "infiniswap" ]; then
git clone https://github.com/juncgu/infiniswap
cd infiniswap
git checkout GUI
else
    cd infiniswap
    git checkout GUI
    git checkout .
    git pull origin GUI
fi