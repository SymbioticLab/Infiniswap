#!/bin/bash

if  [ ! -n "$1" ] ;then
    echo "Usage: ./install.sh {bd/daemon}"
    exit 2
fi

# block device options
#have the kernel patch for lookup_bdev()
#(HAVE_LOOKUP_BDEV_PATCH), default is undefined
have_lookup_bdev_patch=0

#max page number in a single struct request (swap IO request),
#(MAX_SGL_LEN), default is 1 (<4.4.0), 32 (>=4.4.0)
max_page_num=1

#max page number in a single bio request 
#(BIO_PAGE_CAP), default is 32
bio_page_cap=32

#max remote memory size from one server 
#(MAX_MR_SIZE_GB), default is 32
max_remote_memory=32    #also for daemon

#stackbd (backup) disk size, also the total size of remote memory of this bd
#(STACKBD_SIZE), default is 12
stackbd_size=12

#name of stackbd disk
#(STACKBD_NAME), default is "stackbd"
stackbd_name="stackbd"

#name of physical backup disk
#(BACKUP_DISK), default is "/dev/sda4"
backup_disk="/dev/sda4"

#number of queried server in looking for remote memory
#(SERVER_SELECT_NUM), default is 1
num_server_select=1



# daemon options
#how many block devices a single daemon can connect to
#(MAX_CLIENT), default is 32
max_client=32

#maximum size (in GB) of remote memory this daemon can provide
#(MAX_FREE_MEM_GB, MAX_MR_SIZE_GB), default is 32
#max_remote_memory=

#lower threshold of host free memory to evict remote memory chunks
#(FREE_MEM_EVICT_THRESHOLD), default is 8
remote_memory_evict=8

#limit of hitting evict_threshold before triggering eviction
#(MEM_EVICT_HIT_THRESHOLD), default is 1
evict_hit_limit=1

#upper threshold of host free memory to expand remote memory chunks
#(FREE_MEM_EXPAND_THRESHOLD), default is 16
remote_memory_expand=16

#limit of hitting expand_threshold before triggering expansion
#(MEM_EXPAND_HIT_THRESHOLD), default is 20
expand_hit_limit=20

#weight of measured free memory in the weighted moving average for
#current free memory calculation (CURR_FREE_MEM_WEIGHT), default is 0.7
measured_free_mem_weight=0.7

bd_options="--enable-max_page_num=${max_page_num} \
    --enable-bio_page_cap=${bio_page_cap} \
    --enable-max_remote_memory=${max_remote_memory} \
    --enable-stackbd_size=${stackbd_size} \
    --enable-stackbd_name=${stackbd_name} \
    --enable-backup_disk=${backup_disk} \
    --enable-num_server_select=${num_server_select}"
if [ ${have_lookup_bdev_patch} -gt 0 ];then
    bd_options="${bd_options} \
    --enable-lookup_bdev"
fi

daemon_options="--enable-max_client=${max_client} \
    --enable-max_remote_memory=${max_remote_memory} \
    --enable-remote_memory_evict=${remote_memory_evict} \
    --enable-remote_memory_expand=${remote_memory_expand} \
    --enable-evict_hit_limit=${evict_hit_limit} \
    --enable-expand_hit_limit=${expand_hit_limit} \
    --enable-measured_free_mem_weight=${measured_free_mem_weight}"

# build infiniswap block device
if [ $1 == "bd" ]; then
echo "........ install infiniswap block device, options:"
echo "${bd_options}"
cd ../infiniswap_bd
./autogen.sh
./configure ${bd_options}
make 
sudo make install
echo "....... done"
#build infiniswap daemon
elif [ $1 == "daemon" ]; then
echo "........ install infiniswap daemon, options:"
echo "${daemon_options}"
cd ../infiniswap_daemon
./autogen.sh
./configure ${daemon_options}
make 
echo "....... done"
fi