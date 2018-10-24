#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([infiniswap daemon], [0.1], [infiniswap@umich.edu])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])



# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

# Checks for libraries.
AC_CHECK_LIB([ibverbs], [ibv_get_device_list], [], [AC_MSG_ERROR([libibverbs not found])])
AC_CHECK_LIB([pthread], [pthread_create], [], AC_MSG_ERROR([libpthread not found]))
AC_CHECK_LIB([rdmacm], [rdma_create_event_channel], [], AC_MSG_ERROR([librdmacm-devel not found]))

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stdlib.h string.h unistd.h])
AC_CHECK_HEADERS([infiniband/verbs.h], [], [AC_MSG_ERROR([ibverbs header files not found])])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset])

CFLAGS="-Wall -Werror -g"
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
