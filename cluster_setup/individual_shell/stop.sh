#!/bin/bash
set -x
sudo kill $(ps aux | grep "infiniswap" | grep -v grep | awk '{print $2}')
sudo kill $(ps aux | grep "runworker" | grep -v grep | awk '{print $2}')
sudo kill $(ps aux | grep "runserver" | grep -v grep | awk '{print $2}')
sudo kill $(ps aux | grep "master" | grep -v grep | awk '{print $2}')