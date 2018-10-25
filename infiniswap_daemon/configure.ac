#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([infiniswap daemon], [0.1], [infiniswap@umich.edu], [], [https://github.com/SymbioticLab/infiniswap])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])

dnl user-defined arguments and MACROs
AC_ARG_ENABLE([max_client],
[AS_HELP_STRING([--enable-max_client],
[User defines how many block devices a single daemon can connect to, default is 32])],
[user_max_client=${enableval}], [user_max_client=32])

AC_DEFINE_UNQUOTED([USER_MAX_CLIENT], [${user_max_client}], [how many block devices a single daemon can connect to, default is 32])


AC_ARG_ENABLE([max_remote_memory],
[AS_HELP_STRING([--enable-max_remote_memory],
[User specifies the maximum size (in GB) of remote memory this daemon can provide (check memory capacity), default is 32])],
[user_max_remote_memory=${enableval}], [user_max_remote_memory=32])

AC_DEFINE_UNQUOTED([USER_MAX_REMOTE_MEMORY], [${user_max_remote_memory}], [the maximum size (in GB) of remote memory this daemon can provide (MAX_FREE_MEM_GB, MAX_MR_SIZE_GB), default is 32])



AC_ARG_ENABLE([remote_memory_evict],
[AS_HELP_STRING([--enable-remote_memory_evict],
[User defines the lower threshold of host free memory to evict remote memory chunks, default is 8])],
[user_remote_memory_evict=${enableval}], [user_remote_memory_evict=8])

AC_DEFINE_UNQUOTED([USER_REMOTE_MEMORY_EVICT], [${user_remote_memory_evict}], [the lower threshold of host free memory to evict remote memory chunks (FREE_MEM_EVICT_THRESHOLD), default is 8])


AC_ARG_ENABLE([evict_hit_limit],
[AS_HELP_STRING([--enable-evict_hit_limit],
[User defines the limit of hitting evict_threshold before triggering eviction, default is 1])],
[user_evict_hit_limit=${enableval}], [user_evict_hit_limit=1])

AC_DEFINE_UNQUOTED([USER_EVICT_HIT_LIMIT], [${user_evict_hit_limit}], [the limit of hitting evict_threshold before triggering eviction (MEM_EVICT_HIT_THRESHOLD), default is 1])


AC_ARG_ENABLE([remote_memory_expand],
[AS_HELP_STRING([--enable-remote_memory_expand],
[User defines the upper threshold of host free memory to expand remote memory chunks, default is 16])],
[user_remote_memory_expand=${enableval}], [user_remote_memory_expand=16])

AC_DEFINE_UNQUOTED([USER_REMOTE_MEMORY_EXPAND], [${user_remote_memory_expand}], [the upper threshold of host free memory to expand remote memory chunks (FREE_MEM_EXPAND_THRESHOLD), default is 16])


AC_ARG_ENABLE([expand_hit_limit],
[AS_HELP_STRING([--enable-expand_hit_limit],
[User defines the limit of hitting expand_threshold before triggering expansion, default is 20])],
[user_expand_hit_limit=${enableval}], [user_expand_hit_limit=20])

AC_DEFINE_UNQUOTED([USER_EXPAND_HIT_LIMIT], [${user_expand_hit_limit}], [the limit of hitting expand_threshold before triggering expansion (MEM_EXPAND_HIT_THRESHOLD), default is 20])


AC_ARG_ENABLE([measured_free_mem_weight],
[AS_HELP_STRING([--enable-measured_free_mem_weight],
[User defines the weight of measured free memory in the weighted moving average for current free memory calculation, default is 0.7])],
[user_measured_free_mem_weight=${enableval}], [user_measured_free_mem_weight=0.7])

AC_DEFINE_UNQUOTED([USER_MEASURED_FREE_MEM_WEIGHT], [${user_measured_free_mem_weight}], [the weight of measured free memory in the weighted moving average for current free memory calculation (CURR_FREE_MEM_WEIGHT), default is 0.7])


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
