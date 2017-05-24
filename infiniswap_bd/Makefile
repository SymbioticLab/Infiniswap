SHELL = /bin/sh
INSTALL = /usr/bin/install -c
mkdir_p = mkdir -p
VERSION = 0.0
build_dir = /lib/modules/$(shell uname -r)

ismoduledir = /lib/modules/$(shell uname -r)/extra/infiniswap
ismodule := infiniswap.ko
issrc := ./
nbdxadm := nbdxadm

all: all-spec

install: all
	$(mkdir_p) $(DESTDIR)$(ismoduledir)
	$(INSTALL) -m 644 $(issrc)/$(ismodule) $(DESTDIR)$(ismoduledir)/$(ismodule)
	-/sbin/depmod -a
	-\cp $(nbdxadm)/nbdxadm /usr/local/bin/

uninstall:
	rm -f $(DESTDIR)$(ismoduledir)/$(ismodule)
	-/sbin/depmod -a
	-rm -f /usr/local/bin/nbdxadm

clean:
	$(MAKE) -C $(build_dir)/build SUBDIRS=`pwd` clean

obj-m := infiniswap.o

infiniswap-objs := 	\
	is_configfs.o	\
	is_mq.o	\
	is_main.o

all-spec:
	$(MAKE) -C $(build_dir)/build SUBDIRS=`pwd`  modules
