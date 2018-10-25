#!/bin/bash
set -x
sudo kill $(ps aux | grep "infiniswap" | grep -v "stop" | awk '{print $2}')
sudo kill $(ps aux | grep "runworker" | grep -v "stop" | awk '{print $2}')
sudo kill $(ps aux | grep "runserver" | grep -v "stop" | awk '{print $2}')
sudo kill $(ps aux | grep "master" | grep -v "stop" | awk '{print $2}')