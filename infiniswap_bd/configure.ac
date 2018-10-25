AC_INIT([infiniswap block device],[0.1],[infiniswap@umich.edu], [], [https://github.com/SymbioticLab/infiniswap])

AC_PROG_INSTALL

runver=`uname -r`
bad_kernel_version=no
ENABLE_XIO_MODULE=y
ENABLE_OFED_BUILD=y
KERNELCFLAGS=

kernelsrc=
kernelbuild=
AC_ARG_WITH(kernel,
[  --with-kernel=PATH      Specify location of kernel source ],
[kernelsrc="$withval"; kernelbuild="$withval"])
AC_ARG_WITH(kernel-build,
[  --with-kernel-build=PATH Specify location of kernel build ],
[kernelbuild="$withval"])
AC_ARG_ENABLE(kernel-module,
[  --enable-kernel-module  Compile kernel module ])

dnl user-defined arguments and MACROs
AC_ARG_ENABLE([lookup_bdev],
[AS_HELP_STRING([--enable-lookup_bdev],
[Kernel has the patch for lookup_bdev (https://www.redhat.com/archives/dm-devel/2016-April/msg00372.html); Disabled by default])],
[have_lookup_bdev_patch=yes], [have_lookup_bdev_patch=no])

if [test $have_lookup_bdev_patch = yes]; then 
  AC_DEFINE([HAVE_LOOKUP_BDEV_PATCH], [], [Have the kernel patch for lookup_bdev()
  https://www.redhat.com/archives/dm-devel/2016-April/msg00372.html])
fi

AC_ARG_ENABLE([max_page_num],
[AS_HELP_STRING([--enable-max_page_num],
[User specifies the max page number in a single struct request (swap IO request), default is 1 (<4.4.0), 32 (>=4.4.0)])],
[user_max_page_num=${enableval}], [user_max_page_num=1])

AC_DEFINE_UNQUOTED([USER_MAX_PAGE_NUM], [${user_max_page_num}], [the max page number in a single struct request (swap IO request), MAX_SGL_LEN])


AC_ARG_ENABLE([bio_page_cap],
[AS_HELP_STRING([--enable-bio_page_cap],
[User specifies the max page number in a single bio request, default is 32])],
[user_bio_page_cap=${enableval}], [user_bio_page_cap=32])

AC_DEFINE_UNQUOTED([USER_BIO_PAGE_CAP], [${user_bio_page_cap}], [the max page number in a single bio request, default is 32])


AC_ARG_ENABLE([max_remote_memory],
[AS_HELP_STRING([--enable-max_remote_memory],
[User defines the max remote memory size from one server, default is 32])],
[user_max_remote_memory=${enableval}], [user_max_remote_memory=32])

AC_DEFINE_UNQUOTED([USER_MAX_REMOTE_MEMORY], [${user_max_remote_memory}], [the max remote memory size from one server (MAX_MR_SIZE_GB), default is 32])


AC_ARG_ENABLE([stackbd_size],
[AS_HELP_STRING([--enable-stackbd_size],
[User defines the size of stackbd (backup) disk which should be >= the size of remote memory, default is 12])],
[user_stackbd_size=${enableval}], [user_stackbd_size=12])

AC_DEFINE_UNQUOTED([USER_STACKBD_SIZE], [${user_stackbd_size}], [stackbd (backup) disk size], default is 12)


AC_ARG_ENABLE([stackbd_name],
[AS_HELP_STRING([--enable-stackbd_name],
[User defines the name of stackbd (backup) disk, default is "stackbd"])],
[user_stackbd_name="${enableval}"], [user_stackbd_name="stackbd"])

AC_DEFINE_UNQUOTED([USER_STACKBD_NAME], ["${user_stackbd_name}"], [the name of stackbd disk], default is "stackbd")


AC_ARG_ENABLE([backup_disk],
[AS_HELP_STRING([--enable-backup_disk],
[User specifies which physical disk partition will be used for backup, default is "/dev/sda4"])],
[user_backup_disk="${enableval}"], [user_backup_disk="/dev/sda4"])

AC_DEFINE_UNQUOTED([USER_BACKUP_DISK], ["${user_backup_disk}"], [the name of physical backup disk, default is "/dev/sda4"])


AC_ARG_ENABLE([num_server_select],
[AS_HELP_STRING([--enable-num_server_select],
[User defines the number of queried server in looking for remote memory, default is 1])],
[user_num_server_select="${enableval}"], [user_num_server_select=1])

AC_DEFINE_UNQUOTED([USER_NUM_SERVER_SELECT], [${user_num_server_select}], [the number of queried server in looking for remote memory (SERVER_SELECT_NUM), default is 1])

AC_CONFIG_HEADERS([config.h])

#build against installed OFED
RDMA_SYMVERS=`pwd`/Module.symvers

if test "$ENABLE_OFED_BUILD" = "y"; then
AC_MSG_CHECKING([if ofed installed])
MLNX_OFED=`if ofed_info 2>/dev/null | grep MLNX_OFED >/dev/null 2>/dev/null; then echo true; else echo false; fi`
OFED_CFLAGS=

if test "$MLNX_OFED" = "true"; then
AC_MSG_RESULT(yes)

# Whether MLNX_OFED for ubuntu has been installed
MLNX_OFED_IB_UBUNTU_INSTALLED=`if dpkg -s mlnx-ofed-kernel-dkms >/dev/null 2>/dev/null; then echo true; else echo false; fi`

# Whether MLNX_OFED for RedHat has been installed
MLNX_OFED_IB_RH_INSTALLED=`if rpm -q mlnx-ofa_kernel-devel >&/dev/null; then echo true; else echo false; fi`

# Check if we have custom compiled kernel modules
if test "$MLNX_OFED_IB_RH_INSTALLED" = "false"; then
MLNX_OFED_IB_RH_INSTALLED=`if rpm -q kernel-ib-devel >&/dev/null; then echo true; else echo false; fi`
fi

if test "$MLNX_OFED_IB_UBUNTU_INSTALLED" = "true"; then
OFED_VERS=`dpkg -s mlnx-ofed-kernel-dkms | awk -F\- '/Version/ {print $1}' | awk '{print $2}'`
OFED_CFLAGS=`echo -I/var/lib/dkms/mlnx-ofed-kernel/$OFED_VERS/build/include -include /var/lib/dkms/mlnx-ofed-kernel/$OFED_VERS/build/include/linux/compat-2.6.h`
RDMA_SYMVERS=`echo $RDMA_SYMVERS /var/lib/dkms/mlnx-ofed-kernel/$OFED_VERS/build/Module.symvers`
fi

if test "$MLNX_OFED_IB_RH_INSTALLED" = "true"; then
OFED_CFLAGS=`echo -I/usr/src/ofa_kernel/default/include -include /usr/src/ofa_kernel/default/include/linux/compat-2.6.h`
RDMA_SYMVERS=`echo $RDMA_SYMVERS /usr/src/ofa_kernel/default/Module.symvers`
fi
else
AC_MSG_RESULT(no)

# Whether or not the OFED kernel-ib-devel RPM has been installed.
OFED_KERNEL_IB_DEVEL_RPM_INSTALLED=`if rpm -q kernel-ib-devel 2>/dev/null | grep -q $(uname -r | sed 's/-/_/g'); then echo true; else echo false; fi`

# Whether or not the OFED compat-rdma-devel RPM has been installed.
OFED_COMPAT_RDMA_DEVEL_RPM_INSTALLED=`if rpm -q compat-rdma-devel 2>/dev/null | grep -q $(uname -r | sed 's/-/_/g'); then echo true; else echo false; fi`

if test "$OFED_KERNEL_IB_DEVEL_RPM_INSTALLED" = "true"; then
# Read OFED's config.mk, which contains the definition of the variable
# BACKPORT_INCLUDES.
cfile="/usr/src/ofa_kernel/config.mk"
if test -r "${cfile}"; then
  echo "loading build-specific script '${cfile}'"
  . "${cfile}"
else
cfile="/usr/src/ofa_kernel/default/config.mk"
if test -r "${cfile}"; then
echo "loading build-specific script '${cfile}'"
. "${cfile}"
fi
fi

OFED_CFLAGS=`echo $BACKPORT_INCLUDES -I/usr/src/ofa_kernel/include`
RDMA_SYMVERS=`echo $RDMA_SYMVERS /usr/src/ofa_kernel/Module.symvers`
fi

if test "$OFED_COMPAT_RDMA_DEVEL_RPM_INSTALLED" = "true"; then
OFED_CFLAGS=`echo -I/usr/src/compat-rdma/include -include /usr/src/compat-rdma/include/linux/compat-2.6.h`
RDMA_SYMVERS=`echo $RDMA_SYMVERS /usr/src/compat-rdma/Module.symvers`
fi
fi
AC_SUBST(OFED_CFLAGS)
AC_SUBST(RDMA_SYMVERS)
fi


AC_MSG_CHECKING([kernel source directory])
if test -z "$kernelsrc"; then
kernelbuild=
sourcelink=/lib/modules/${runver}/source
buildlink=/lib/modules/${runver}/build

if test -e $sourcelink; then
kernelsrc=`(cd $sourcelink; /bin/pwd)`
fi
if test -e $buildlink; then
kernelbuild=`(cd $buildlink; /bin/pwd)`
fi
if test -z "$kernelsrc"; then
kernelsrc=$kernelbuild
fi
if test -z "$kernelsrc" -o -z "$kernelbuild"; then
AC_MSG_RESULT([Not found])
AC_MSG_ERROR([
*** Please specify the location of the kernel source with
*** the '--with-kernel=SRCDIR' option])
fi

AC_MSG_RESULT([$kernelsrc])
AC_MSG_CHECKING([kernel build directory])
AC_MSG_RESULT([$kernelbuild])

AC_MSG_CHECKING([kernel source version])
if test -r $kernelbuild/include/linux/version.h && fgrep -q UTS_RELEASE $kernelbuild/include/linux/version.h; then
kernsrcver=`(echo "#include <linux/version.h>"; echo "kernsrcver=UTS_RELEASE") | cpp -I $kernelbuild/include | grep "^kernsrcver=" | cut -d \" -f 2`
elif test -r $kernelbuild/include/linux/utsrelease.h && fgrep -q UTS_RELEASE $kernelbuild/include/linux/utsrelease.h; then
kernsrcver=`(echo "#include <linux/utsrelease.h>"; echo "kernsrcver=UTS_RELEASE") | cpp -I $kernelbuild/include | grep "^kernsrcver=" | cut -d \" -f 2`
elif test -r $kernelbuild/include/generated/utsrelease.h && fgrep -q UTS_RELEASE $kernelbuild/include/generated/utsrelease.h; then
kernsrcver=`(echo "#include <generated/utsrelease.h>"; echo "kernsrcver=UTS_RELEASE") | cpp -I $kernelbuild/include | grep "^kernsrcver=" | cut -d \" -f 2`
fi
if test -z "$kernsrcver"; then
AC_MSG_RESULT([Not found])
AC_MSG_ERROR([
*** Cannot determine the version of the linux kernel source. Please
*** prepare the kernel before running this script])
fi
AC_MSG_RESULT([$kernsrcver])
kmoduledir=${INSTALL_MOD_PATH}/lib/modules/$kernsrcver
AC_SUBST(kernelsrc)
AC_SUBST(kmoduledir)

KERNELMAKE_PARAMS=
KERNELCPPFLAGS=
AC_MSG_CHECKING([if this is user mode linux])
if test -f $kernelbuild/include/linux/autoconf.h && egrep -q "^#define CONFIG_(USERMODE|UML) 1" $kernelbuild/include/linux/autoconf.h; then
isuml=yes
KERNELMAKE_PARAMS="ARCH=um"
KERNELCPPFLAGS="-D__arch_um__ -DSUBARCH=\\\"i386\\\" -D_LARGEFILE64_SOURCE -I${kernelsrc}/arch/um/include -Derrno=kernel_errno -I${kernelsrc}/arch/um/kernel/tt/include -I${kernelsrc}/arch/um/kernel/skas/include"
fi
AC_MSG_RESULT([$isuml])
if test "$kernelbuild" != "$kernelsrc"; then
KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$kernelbuild"
fi
AC_SUBST(KERNELMAKE_PARAMS)
AC_SUBST(KERNELCPPFLAGS)
AC_SUBST(KERNELCFLAGS)
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
