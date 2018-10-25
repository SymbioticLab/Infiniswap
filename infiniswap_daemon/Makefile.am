AUTOMAKE_OPTIONS = subdir-objects

noinst_LIBRARIES = librdma.a
librdma_a_SOURCES = rdma-common.c
noinst_HEADERS = rdma-common.h

bin_PROGRAMS = infiniswap-daemon
infiniswap_daemon_SOURCES = infiniswap-daemon.c
infiniswap_daemon_LDADD = librdma.a