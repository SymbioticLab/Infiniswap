all: master worker

master: master.cpp
	g++ -o master master.cpp -std=c++11 -I /usr/include/mysql -L /usr/lib/mysql -lmysqlclient

worker: worker.cpp
	g++ -o worker worker.cpp -std=c++11 -pthread

clean:
	rm -f master worker