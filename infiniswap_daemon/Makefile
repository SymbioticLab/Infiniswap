.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := infiniswap-daemon

all: ${APPS}

gui: rdma-common.o infiniswap-daemon.o
	${LD} -D IS_GUI -o infiniswap-daemon $^ ${LDLIBS}

infiniswap-daemon: rdma-common.o infiniswap-daemon.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}

