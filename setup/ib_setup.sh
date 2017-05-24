#!/bin/sh  
#specify ip 
sudo modprobe ib_ipoib
sudo ifconfig ib0 $1/24