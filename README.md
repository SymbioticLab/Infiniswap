Infiniswap: Efficient Memory Disaggregation
====

Infiniswap is a remote memory paging system designed specifically for an RDMA network.
It opportunistically harvests and transparently exposes unused memory to unmodified applications by dividing the swap space of each machine into many slabs and distributing them across many machines' remote memory. 
Because one-sided RDMA operations bypass remote CPUs, Infiniswap leverages the power of many choices to perform decentralized slab placements and evictions.

Extensive benchmarks on workloads from memory-intensive applications ranging  from in-memory databases such as VoltDB and Memcached to popular big data software Apache Spark, PowerGraph, and GraphX show that Infiniswap provides *order-of-magnitude performance improvements* when working sets do not completely fit in memory. 
Simultaneously, it boosts cluster memory utilization by almost 50%. 

Detailed design and performance benchmarks are available in our [NSDI'17 paper](https://www.usenix.org/conference/nsdi17/technical-sessions/presentation/gu).

Prerequisites
-----------

The following prerequisites are required to use Infiniswap:  

* Software  
  * Operating system: Ubuntu 14.04 (kernel 3.13.0, also tested on 4.4.0/4.11.0)
  * Container: LXC (or any other container technologies) with cgroup (memory and swap) enabled  
  * RDMA NIC driver: [MLNX_OFED 3.2/3.3/3.4/4.1](http://www.mellanox.com/page/products_dyn?product_family=26), and select the right version for your operating system. 

* Hardware  
   * Mellanox ConnectX-3/4 (InfiniBand)
   * An empty and unused disk partition

Code Organization
-----------
The Infiniswap codebase is organized under three directories.

* `infiniswap_bd`: Infiniswap block device (kernel module).
* `infiniswap_daemon`: Infiniswap daemon (user-level process) that exposes its local memory as remote memory.
* `setup`: setup scripts.

Important Parameters
-----------

Some important parameters in Infiniswap:  

* `infiniswap_bd/infiniswap.h`
  * `BACKUP_DISK` [disk partition]  
    It's the name of the backup disk in Infiniswap block device.  
    How to check the disk partition status and list?  
      "sudo fdisk -l"   
  * `STACKBD_SIZE_G` [size in GB]  
    It defines the size of Infiniswap block device (also backup disk).   
  * `MAX_SGL_LEN` [num of pages]  
    It specifies how many pages can be included in a single swap-out request (IO request).  
  * `BIO_PAGE_CAP` [num of pages]  
    It limits the maximum value of MAX_SGL_LEN.  
  * `MAX_MR_SIZE_GB` [size]  
    It sets the maximum number of slabs from a single Infiniswap daemon. Each slab is 1GB.
```c
// example, in "infiniswap.h" 
#define BACKUP_DISK "/dev/sda4"  
#define STACKBD_SZIE_G 12  // 12GB
#define MAX_SGL_LEN 32  // 32 x 4KB = 128KB, it's the max size for a single "struct bio" object.
#define BIO_PAGE_CAP 32
#define MAX_MR_SIZE_GB 32 //this infiniswap block device can get 32 slabs from each infiniswap daemon.
```

* `infiniswap_daemon/rdma-common.h`
  * `MAX_FREE_MEM_GB` [size]   
    It is the maximum size (in GB) of remote memory this daemon can provide (from free memory of the local host).     
  * `MAX_MR_SIZE_GB` [size]   
    It limits the maximum number of slabs this daemon can provide to a single infiniswap block device.   
    This value should be the same of "MAX_MR_SIZE_GB" in "infiniswap.h".    
  * `MAX_CLIENT` [number]   
    It defines how many infiniswap block devices a single daemon can connect to.     
  * `FREE_MEM_EVICT_THRESHOLD` [size in GB]   
    This is the "HeadRoom" mentioned in our paper.   
    When the remaining free memory of the host machines is lower than this threshold, infiniswap daemon will start to evict mapped slabs.     
```c
// example, in "rdma-common.h" 
#define MAX_CLIENT 32     

/* Followings should be assigned based on 
 * memory information (DRAM capacity, regular memory usage, ...) 
 * of the host machine of infiniswap daemon.    
 */
#define MAX_FREE_MEM_GB 32    
#define MAX_MR_SIZE_GB  32    
#define FREE_MEM_EVICT_THRESHOLD 8    
```

How to Build and Install
-----------

In a simple one-to-one experiment, we have two machines (M1 and M2).  
Applications run in container on M1. 
M1 needs remote memory from M2.  
We need to install infiniswap block device on M1, and install infiniswap daemon on M2.  

1. Setup InfiniBand NIC on both machines:  
```bash  
cd setup  
# ./ib_setup.sh <ip>    
# assume all IB NICs are connected in the same LAN (192.168.0.x)
# M1:192.168.0.11, M2:192.168.0.12
sudo ./ib_setup.sh 192.168.0.11
```
2. Compile infiniswap daemon on M2:
```bash  
cd infiniswap_daemon
make
```
3. Install infiniswap block device on M1:  
```bash  	
cd infiniswap_bd  
./autogen.sh
./configure
make  
sudo make install
```

How to Run
-----------
1. Start infiniswap daemon on M2:  
```bash  	
cd infiniswap_daemon   
# ./infiniswap-daemon <ip> <port> 
# pick up an unused port number
./infiniswap-daemon 192.168.0.12 9400
```
2. Prepare server (portal) list on M1:  
```  
# Edit the port.list file (<infiniswap path>/setup/portal.list)
# portal.list format, the port number of each server is assigned above.  
Line1: number of servers
Line2: <server1 ip>:<port>  
Line3: <server2 ip>:<port>
Line4: ...
```
```bash  
# in this example, M1 only has one server
1
192.168.0.12:9400
```
3. Disable existing swap partitions on M1:
```bash  	
# check existing swap partitions
sudo swapon -s

# disable existing swap partitions
sudo swapoff <swap partitions>
```
4. Create an infiniswap block device on M1:  
```bash  	
cd setup
# create block device: nbdx-infiniswap0
# make nbdx-infiniswap0 a swap partition
sudo ./infiniswap_bd_setup.sh
```

```bash  	
# If you have the error: 
#   "insmod: ERROR: could not insert module infiniswap.ko: Invalid parameters"
# or get the following message from kernel (dmesg):
#   "infiniswap: disagrees about version of symbol: xxxx"
# You need a proper Module.symvers file for the MLNX_OFED driver
#
cd infiniswap_bd
make clean
cd ../setup
# Solution 1 (copy the Module.symvers file from MLNX_OFED):
./get_module.symvers.sh
# Or solution 2 (generate a new Module.symvers file)
#./create_Module.symvers.sh
# Then, recompile infiniswap block device from step 3 in "How to Build and Install"
```

5. Configure memory limitation of container (LXC)  
```bash  	
# edit "memory.limit_in_bytes" in "config" file of container (LXC)

# For example, this container on M1 can use 5GB local memory at most.
# Additional memory data will be stored in the remote memory provided by M2.   
lxc.cgroup.memory.limit_in_bytes = 5G
```

Now, you can start your applications (in container).     
The extra memory data from applications will be stored in remote memory.   

FAQ
----------
1. Does infiniswap support transparent huge page?   
**Yes.**
Infiniswap relies on the swap mechanism in the original Linux kernel.
Current kernel (we have tested up to 4.10) splits the huge page into basic pages (4KB) before swapping out the huge page.  
(In `mm/vmscan.c`, `shrink_page_list()` calls `split_huge_page_to_list()` to split the huge page.)   
Therefore, whether transparent huge page is enabled or not makes no difference for infiniswap.   

2. Can we use Docker container, other than LXC?    
**Yes.**
Infiniswap requires container-based environment. 
However, it has no dependency on LXC. Any container technologies that can limit memory resource and enable swapping should be feasible.  
We haven't tried Docker yet. If you find any problems when running infiniswap in a Docker environment, please contact us.  

3. Invalid parameters error when insert module?
There are two ways of compiling infiniswap; using 1) inbox driver 2) Mellanox OFED
When you use inbox driver, you can compile/link against kernel headers/modules.
When you use Mellanox OFED, you need to compile/link against OFED headers/modules.
This should be handled by configure file, and refer the Makefile that links OFED modules.

4. Others issues about compatibility
    1. `lookup_bdev()` has two arguments in the [kernel patch](https://www.redhat.com/archives/dm-devel/2016-April/msg00372.html)
      Turn on `LOOKUP_BDEV_PATCH` at line 49 in ``is_mq.c`` if your OS has this patch; otherwise, just comment it out.


    

Contact
-----------
This work is by [Juncheng Gu](http://web.eecs.umich.edu/~jcgu/), Youngmoon Lee, Yiwen Zhang, [Mosharaf Chowdhury](http://www.mosharaf.com/), and [Kang G. Shin](https://web.eecs.umich.edu/~kgshin/). 
You can email us at `infiniswap at umich dot edu`, file issues, or submit pull requests.
