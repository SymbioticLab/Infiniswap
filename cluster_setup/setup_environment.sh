#!/bin/bash

set -e 
set -x

wget https://s3-us-west-2.amazonaws.com/grafana-releases/release/grafana_5.1.4_amd64.deb
sudo apt-get install -y adduser libfontconfig
sudo dpkg -i grafana_5.1.4_amd64.deb
sudo apt-get update
sudo apt-get install grafana
sudo service grafana-server start
sudo grafana-cli plugins install grafana-piechart-panel
sudo debconf-set-selections <<< 'mysql-server mysql-server/root_password password mysql'
sudo debconf-set-selections <<< 'mysql-server mysql-server/root_password_again password mysql'
sudo apt-get -y install mysql-server
mysql -u root -p"mysql" < db.sql
sudo service mysql start
sudo apt-get install -y nodejs
sudo apt-get install npm
sudo npm cache clean -f
npm config set strict-ssl false
sudo npm install -g n
sudo n latest
npm install node-ipc
npm install socket.io
python grafana_data.py
python grafana_dashboard.py
node main.js