/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 *  
 * Copyright (c) 2013 Mellanox Technologies��. All rights reserved.
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the Mellanox Technologies�� BSD license
 * below:
 *
 *      - Redistribution and use in source and binary forms, with or without
 *        modification, are permitted provided that the following conditions
 *        are met:
 *
 *      - Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither the name of the Mellanox Technologies�� nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef INFINISWAP_H
#define INFINISWAP_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
#include <asm/atomic.h>
#else
#include <linux/atomic.h>
#endif
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/fcntl.h>
#include <linux/cpumask.h>
#include <linux/configfs.h>
#include <linux/delay.h>

#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/proc_fs.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/pci.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

//for stackbd
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <trace/events/block.h>

#include "config.h"

// from NBDX
#define SUBMIT_BLOCK_SIZE				\
	+ sizeof(uint32_t) /* raio_filedes */		\
	+ sizeof(uint32_t) /* raio_lio_opcode */	\
	+ sizeof(uint64_t) /* nbytes */			\
	+ sizeof(uint64_t) /* offset */

#define STAT_BLOCK_SIZE					\
	+ sizeof(uint64_t) /* dev */			\
	+ sizeof(uint64_t) /* ino */			\
	+ sizeof(uint32_t) /* mode */			\
	+ sizeof(uint32_t) /* nlink */                  \
	+ sizeof(uint64_t) /* uid */			\
	+ sizeof(uint64_t) /* gid */			\
	+ sizeof(uint64_t) /* rdev */			\
	+ sizeof(uint64_t) /* size */                   \
	+ sizeof(uint32_t) /* blksize */                \
	+ sizeof(uint32_t) /* blocks */                 \
	+ sizeof(uint64_t) /* atime */                  \
	+ sizeof(uint64_t) /* mtime */                  \
	+ sizeof(uint64_t) /* ctime */
struct raio_answer {
	uint32_t command;
	uint32_t data_len;
	int32_t ret;
	int32_t ret_errno;
};
struct raio_command {
	uint32_t command;
	uint32_t data_len;
};
struct raio_iocb_common {
	void			*buf;
	unsigned long long	nbytes;
	long long		offset;
	unsigned int		flags;
	unsigned int		resfd;
};	
struct raio_iocb {
	void			*data;  /* Return in the io completion event */
	unsigned int		key;	/* For use in identifying io requests */
	int			raio_fildes;
	int			raio_lio_opcode;
	int			pad;
	union {
		struct raio_iocb_common	c;
	} u;
};

#define LAST_IN_BATCH sizeof(uint32_t)

#define SUBMIT_HEADER_SIZE (SUBMIT_BLOCK_SIZE +	    \
			    LAST_IN_BATCH +	    \
			    sizeof(struct raio_command))

#ifdef USER_MAX_PAGE_NUM
	#define MAX_SGL_LEN USER_MAX_PAGE_NUM	/* max pages in a single struct request (swap IO request) */
#else
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		#define MAX_SGL_LEN 1	/* kernel 4.x only supports single page request*/
	#else
		#define MAX_SGL_LEN 32	/* max pages in a single struct request (swap IO request) */
	#endif
#endif
struct raio_io_u {
	struct scatterlist  sgl[MAX_SGL_LEN];
	struct raio_iocb		iocb;
	struct request		       *breq;
	//struct xio_msg			req;
	//struct xio_msg		       *rsp;
	int				res;
	int				res2;
	struct raio_answer		ans;
	struct list_head		list;

	char				req_hdr[SUBMIT_HEADER_SIZE];
};

// from kernel 
/*  host to network long long
 *  endian dependent
 *  http://www.bruceblinn.com/linuxinfo/ByteOrder.html
 */
#define ntohll(x) (((uint64_t)(ntohl((int)((x << 32) >> 32))) << 32) | \
		    (unsigned int)ntohl(((int)(x >> 32))))
#define htonll(x) ntohll(x)

#define htonll2(x) cpu_to_be64((x))
#define ntohll2(x) cpu_to_be64((x))


#define MAX_MSG_LEN	    512
#define MAX_PORTAL_NAME	  1024
#define MAX_IS_DEV_NAME   256
#define SUPPORTED_DISKS	    256
#define SUPPORTED_PORTALS   5
#define IS_SECT_SIZE	    512
#define IS_SECT_SHIFT	    ilog2(IS_SECT_SIZE)
#define IS_QUEUE_DEPTH    256
#define QUEUE_NUM_MASK	0x001f	//used in addr->(mapping)-> rdma_queue in IS_main.c

//backup disk / swap space  size (GB)
#ifdef USER_STACKBD_SIZE
	#define STACKBD_SIZE_G	USER_STACKBD_SIZE
#else
	#define STACKBD_SIZE_G	12
#endif

#ifdef USER_BACKUP_DISK
	#define BACKUP_DISK	USER_BACKUP_DISK
#else
	#define BACKUP_DISK	"/dev/sda4"
#endif

//how may pages can be added into a single bio (128KB = 32 x 4KB)
#ifdef USER_BIO_PAGE_CAP
	#define BIO_PAGE_CAP	USER_BIO_PAGE_CAP
#else
	#define BIO_PAGE_CAP	32
#endif


#define STACKBD_REDIRECT_OFF 0
#define STACKBD_REDIRECT_ON  1
#define STACKBD_BDEV_MODE (FMODE_READ | FMODE_WRITE | FMODE_EXCL)
#define KERNEL_SECTOR_SIZE 512
#define STACKBD_DO_IT _IOW( 0xad, 0, char * )
#ifdef USER_STACKBD_NAME
	#define STACKBD_NAME USER_STACKBD_NAME
#else
	#define STACKBD_NAME "stackbd"
#endif
#define STACKBD_NAME_0 STACKBD_NAME "0"

static struct stackbd_t {
    sector_t capacity; 
    struct gendisk *gd;
    spinlock_t lock;
    struct bio_list bio_list;
    struct task_struct *thread;
    int is_active;
    struct block_device *bdev_raw;
    struct request_queue *queue;
    atomic_t redirect_done;
} stackbd;

static int major_num = 0;
module_param(major_num, int, 0);
static int LOGICAL_BLOCK_SIZE = 512;
module_param(LOGICAL_BLOCK_SIZE, int, 0);

static DECLARE_WAIT_QUEUE_HEAD(req_event);

//bitmap
#define INT_BITS 32
#define BITMAP_SHIFT 5 // 2^5=32
#define ONE_GB_SHIFT 30
#define BITMAP_MASK 0x1f // 2^5=32
#define ONE_GB_MASK 0x3fffffff
#define ONE_GB 1073741824 //1024*1024*1024 
#define BITMAP_INT_SIZE 8192 //bitmap[], 1GB/4k/32

enum mem_type {
	DMA = 1,
	FASTREG = 2,
	MW = 3,
	MR = 4
};

//max_size from one server or max_size one server can provide
#ifdef USER_MAX_REMOTE_MEMORY
	#define MAX_MR_SIZE_GB USER_MAX_REMOTE_MEMORY
#else
	#define MAX_MR_SIZE_GB 32
#endif

struct IS_rdma_info {
  	uint64_t buf[MAX_MR_SIZE_GB];
  	uint32_t rkey[MAX_MR_SIZE_GB];
  	int size_gb;	
	enum {
		DONE = 1,
		INFO,
		INFO_SINGLE,
		FREE_SIZE,
		EVICT,
		ACTIVITY,
		STOP,
		BIND,
		BIND_SINGLE,
		QUERY
	} type;
};

enum test_state { 
	IDLE = 1,
	CONNECT_REQUEST,
	ADDR_RESOLVED,
	ROUTE_RESOLVED,
	CONNECTED,		// updated by IS_cma_event_handler()
	FREE_MEM_RECV,
	AFTER_FREE_MEM,
	RDMA_BUF_ADV,   // designed for server
	WAIT_OPS,
	RECV_STOP,
	RECV_EVICT,
	RDMA_WRITE_RUNNING,
	RDMA_READ_RUNNING,
	SEND_DONE,
	RDMA_DONE,
	RDMA_READ_ADV,	// updated by IS_cq_event_handler()
	RDMA_WRITE_ADV,
	CM_DISCONNECT,
	ERROR
};

#define IS_PAGE_SIZE 4096

// 1GB remote chunk struct	("chunk": we use the term "slab" in our paper)
struct remote_chunk_g {
	uint32_t remote_rkey;		/* remote guys RKEY */
	uint64_t remote_addr;		/* remote guys TO */
	//uint64_t remote_len;		/* remote guys LEN */
	int *bitmap_g;	//1GB bitmap
};

#define CHUNK_MAPPED 1
#define CHUNK_UNMAPPED 0

// struct for write operation
struct chunk_write{
	struct kernel_cb *cb;
	int cb_index;
	int chunk_index;
	struct remote_chunk_g *chunk;	
	unsigned long chunk_offset;
	unsigned long len;
	unsigned long req_offset;
};

enum chunk_list_state {
	C_IDLE,
	C_READY,
	C_EVICT,
	C_STOP,
	// C_OFFLINE	
};

struct remote_chunk_g_list {
	struct remote_chunk_g **chunk_list;
	atomic_t *remote_mapped; 
	int chunk_size_g; //size = chunk_num * ONE_GB
	int target_size_g; // == future size of remote
	int shrink_size_g;
	int *chunk_map;	//cb_chunk_index to session_chunk_index
	struct task_struct *evict_handle_thread;
	char *evict_chunk_map;
	wait_queue_head_t sem;      	
	enum chunk_list_state c_state;
};

/*
 *  rdma kernel Control Block struct.
 */
struct kernel_cb {
	int cb_index; //index in IS_sess->cb_list
	struct IS_session *IS_sess;
	int server;			/* 0 iff client */
	struct ib_cq *cq;
	struct ib_pd *pd;
	struct ib_qp *qp;

	enum mem_type mem;
	struct ib_mr *dma_mr;

	// memory region
	struct ib_recv_wr rq_wr;	/* recv work request record */
	struct ib_sge recv_sgl;		/* recv single SGE */
	struct IS_rdma_info recv_buf;/* malloc'd buffer */
	u64 recv_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(recv_mapping)
	struct ib_mr *recv_mr;

	struct ib_send_wr sq_wr;	/* send work requrest record */
	struct ib_sge send_sgl;
	struct IS_rdma_info send_buf;/* single send buf */
	u64 send_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(send_mapping)
	struct ib_mr *send_mr;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	struct ib_rdma_wr rdma_sq_wr;	/* rdma work request record */
#else
	struct ib_send_wr rdma_sq_wr;	/* rdma work request record */
#endif
	struct ib_sge rdma_sgl;		/* rdma single SGE */
	char *rdma_buf;			/* used as rdma sink */
	u64  rdma_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(rdma_mapping)
	struct ib_mr *rdma_mr;

	// peer's addr info pay attention
	//uint32_t remote_rkey;		/* remote guys RKEY */
	//uint64_t remote_addr;		/* remote guys TO */
	uint64_t remote_len;		/* remote guys LEN */
	struct remote_chunk_g_list remote_chunk;

	char *start_buf;		/* rdma read src */
	u64  start_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(start_mapping)
	struct ib_mr *start_mr;

	enum test_state state;		/* used for cond/signalling */
	wait_queue_head_t sem;      // semaphore for wait/wakeup
	//struct IS_stats stats;

	// from arg
	uint16_t port;			/* dst port in NBO */
	u8 addr[16];			/* dst addr in NBO */
	char *addr_str;			/* dst addr string */
	uint8_t addr_type;		/* ADDR_FAMILY - IPv4/V6 */
	int verbose;			/* verbose logging */
	int size;			/* ping data size */
	int txdepth;			/* SQ depth */
	int local_dma_lkey;		/* use 0 for lkey */

	/* CM stuff  connection management*/
	struct rdma_cm_id *cm_id;	/* connection on client side,*/
	struct rdma_cm_id *child_cm_id;	/* connection on client side,*/
					/* listener on server side. */
	struct list_head list;	
};


enum IS_dev_state {
	DEVICE_INITIALIZING,
	DEVICE_OPENNING,
	DEVICE_RUNNING,
	DEVICE_OFFLINE
};

#define CTX_IDLE		0
#define CTX_R_IN_FLIGHT	1
#define CTX_W_IN_FLIGHT	2

struct rdma_ctx {
	struct IS_connection *IS_conn;
	struct free_ctx_pool *free_ctxs;  //or this one
	//struct mutex ctx_lock;	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	struct ib_rdma_wr rdma_sq_wr;	/* rdma work request record */
#else
	struct ib_send_wr rdma_sq_wr;	/* rdma work request record */
#endif
	struct ib_sge rdma_sgl;		/* rdma single SGE */
	char *rdma_buf;			/* used as rdma sink */
	u64  rdma_dma_addr;
	DECLARE_PCI_UNMAP_ADDR(rdma_mapping)
	struct ib_mr *rdma_mr;
	struct request *req;
	int chunk_index;
	struct kernel_cb *cb;
	unsigned long offset;
	unsigned long len;
	struct remote_chunk_g *chunk_ptr;
	atomic_t in_flight; //true = 1, false = 0
};

struct free_ctx_pool {
	unsigned int len;
	struct rdma_ctx **ctx_list;
	int head;
	int tail;
	spinlock_t ctx_lock;
};
struct ctx_pool_list {
	struct rdma_ctx 	*ctx_pool;
	struct free_ctx_pool *free_ctxs;
};

/*  connection object
 */
struct IS_connection {
	struct kernel_cb		**cbs;
	struct IS_session    *IS_sess;
	//struct xio_context     *ctx;
	//struct xio_connection  *conn;
	struct task_struct     *conn_th;
	int			cpu_id;
	int			wq_flag;
	//struct xio_msg		req;
	//struct xio_msg	       *rsp;
	wait_queue_head_t	wq;

	struct ctx_pool_list **ctx_pools;
	struct rdma_ctx 	*ctx_pool;
	struct free_ctx_pool *free_ctxs;
	//struct ib_send_wr *rdma_sq_wr_pool;
	//char *rdma_buf_pool;
};

struct IS_portal {
	uint16_t port;			/* dst port in NBO */
	u8 addr[16];			/* dst addr in NBO */
};
enum cb_state {
	CB_IDLE=0,
	CB_CONNECTED,	//connected but not mapped 
	CB_MAPPED,
	CB_EVICTING,
	CB_FAIL
};

// added for RDMA_CONNECTION failure handling.
#define DEV_RDMA_ON		1
#define DEV_RDMA_OFF	0

//  server selection, call m server each time.
#ifdef USER_NUM_SERVER_SELECT
	#define SERVER_SELECT_NUM USER_NUM_SERVER_SELECT
#else
	#define SERVER_SELECT_NUM 1
#endif

struct IS_session {
	// Nov19 request distribution
	unsigned long int *read_request_count;	//how many requests on each CPU
	unsigned long int *write_request_count;	//how many requests on each CPU

	//struct kernel_cb 		*cb;	// binding with kernel RDMA
	int mapped_cb_num;	//How many cbs are remote mapped
	struct kernel_cb	**cb_list;	
	struct IS_portal *portal_list;
	int cb_num;	//num of possible servers
	enum cb_state *cb_state_list; //all cbs state: not used, connected, failure

	struct IS_file 		*xdev;	// each session only creates a single IS_file
	//struct xio_session	     *session;
	struct IS_connection	    **IS_conns;

	char			      portal[MAX_PORTAL_NAME];

	struct list_head	      list;
	struct list_head	      devs_list; /* list of struct IS_file */
	spinlock_t		      devs_lock;
	struct config_group	      session_cg;
	struct completion	      conns_wait;
	atomic_t		      conns_count;
	atomic_t		      destroy_conns_count;

	unsigned long long    capacity;
	unsigned long long 	  mapped_capacity;
	int 	capacity_g;

	atomic_t 	*cb_index_map;  //unmapped==-1, this chunk is mapped to which cb
	int *chunk_map_cb_chunk; //sess->chunk map to cb-chunk
	int *unmapped_chunk_list;
	int free_chunk_index; //active header of unmapped_chunk_list
	atomic_t	rdma_on;	//DEV_RDMA_ON/OFF

	struct task_struct     *rdma_trigger_thread; //based on swap rate
	unsigned long write_ops[STACKBD_SIZE_G];
	unsigned long read_ops[STACKBD_SIZE_G];	
	unsigned long last_ops[STACKBD_SIZE_G];
	unsigned long trigger_threshold;
	spinlock_t write_ops_lock[STACKBD_SIZE_G];
	spinlock_t read_ops_lock[STACKBD_SIZE_G];
	int w_weight;
	int cur_weight;
	atomic_t trigger_enable;
};
#define TRIGGER_ON 1
#define TRIGGER_OFF 0

#define RDMA_TRIGGER_PERIOD 1000  //1 second
#define RDMA_TRIGGER_THRESHOLD 0 
#define RDMA_W_WEIGHT 50
#define RDMA_CUR_WEIGHT 80

#define NO_CB_MAPPED -1
// #define NUM_CB 1		moved to is_main.c


struct IS_queue {
	unsigned int		     queue_depth;
	struct IS_connection	    *IS_conn;
	struct IS_file	    *xdev; /* pointer to parent*/
};



struct r_stat64 {
    uint64_t     st_size;    /* total size, in bytes */
 };

struct IS_file {
	int			     fd;
	int			     major; /* major number from kernel */
	struct r_stat64		     stbuf; /* remote file stats*/
	char			     file_name[MAX_IS_DEV_NAME];
	struct list_head	     list; /* next node in list of struct IS_file */
	struct gendisk		    *disk;
	struct request_queue	    *queue; /* The device request queue */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	struct blk_mq_tag_set	     tag_set;
#endif
	struct IS_queue	    *queues;
	unsigned int		     queue_depth;
	unsigned int		     nr_queues;
	int			     index; /* drive idx */
	char			     dev_name[MAX_IS_DEV_NAME];
	struct IS_connection	    **IS_conns;
	struct config_group	     dev_cg;
	spinlock_t		     state_lock;
	enum IS_dev_state	     state;	
};

#define uint64_from_ptr(p)    (uint64_t)(uintptr_t)(p)
#define ptr_from_uint64(p)    (void *)(unsigned long)(p)

extern struct list_head g_IS_sessions;
extern struct mutex g_lock;
extern int created_portals;
extern int submit_queues;
extern int IS_major;
extern int IS_indexes;

int IS_single_chunk_map(struct IS_session *IS_session, int i);
int IS_transfer_chunk(struct IS_file *xdev, struct kernel_cb *cb, int cb_index, int chunk_index, struct remote_chunk_g *chunk, unsigned long offset,
		  unsigned long len, int write, struct request *req,
		  struct IS_queue *q);
int IS_session_create(const char *portal, struct IS_session *IS_session);
void IS_session_destroy(struct IS_session *IS_session);
int IS_create_device(struct IS_session *IS_session,
		       const char *xdev_name, struct IS_file *IS_file);
void IS_destroy_device(struct IS_session *IS_session,
                         struct IS_file *IS_file);
int IS_register_block_device(struct IS_file *IS_file);
void IS_unregister_block_device(struct IS_file *IS_file);
int IS_setup_queues(struct IS_file *xdev);
void IS_destroy_queues(struct IS_file *xdev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
blk_qc_t stackbd_make_request(struct request_queue *q, struct bio *bio);
#else
void stackbd_make_request(struct request_queue *q, struct bio *bio);
#endif
void stackbd_make_request2(struct request_queue *q, struct request *req);
void stackbd_make_request3(struct request_queue *q, struct request *req);
void stackbd_make_request4(struct request_queue *q, struct request *req);
void stackbd_make_request5(struct bio *b);
void IS_mq_request_stackbd(struct request *req);
void IS_mq_request_stackbd2(struct request *req);
void IS_single_chunk_init(struct kernel_cb *cb);
void IS_chunk_list_init(struct kernel_cb *cb);
void IS_bitmap_set(int *bitmap, int i);
bool IS_bitmap_test(int *bitmap, int i);
void IS_bitmap_clear(int *bitmap, int i);
void IS_bitmap_init(int *bitmap);
void IS_bitmap_group_set(int *bitmap, unsigned long offset, unsigned long len);
void IS_bitmap_group_clear(int *bitmap, unsigned long offset, unsigned long len);
void IS_insert_ctx(struct rdma_ctx *ctx);

//configfs
int IS_create_configfs_files(void);
void IS_destroy_configfs_files(void);
struct IS_file *IS_file_find(struct IS_session *IS_session,
				 const char *name);
struct IS_session *IS_session_find_by_portal(struct list_head *s_data_list,
						 const char *portal);
const char* IS_device_state_str(struct IS_file *dev);
int IS_set_device_state(struct IS_file *dev, enum IS_dev_state state);

#endif  /* INFINISWAP_H */

