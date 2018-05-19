/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 * GPLv2 License
 */
#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/kernel.h>

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

#define CQ_QP_BUSY 1
#define CQ_QP_IDLE 0
#define CQ_QP_DOWN 2

#define MAX_CLIENT	32
#define EXTRA_CHUNK_NUM 2

#define MAX_FREE_MEM_GB 32 //for local memory management
#define MAX_MR_SIZE_GB 32 //for msg passing

#define ONE_GB 0x40000000 //1024*1024*1024
#define ONE_MB 0x100000 //1024*1024*1024
#define SLAB_SIZE ONE_MB


#define FREE_MEM_EVICT_THRESHOLD 1 //in GB
#define FREE_MEM_EXPAND_THRESHOLD 20 // in GB
#define CURR_FREE_MEM_WEIGHT 0.7
#define MEM_EVICT_HIT_THRESHOLD 1
#define MEM_EXPAND_HIT_THRESHOLD 20


#define ntohll(x) (((uint64_t)(ntohl((int)((x << 32) >> 32))) << 32) | \
        (unsigned int)ntohl(((int)(x >> 32))))

enum mode {
  M_WRITE,
  M_READ
};


struct message {
  uint64_t buf[MAX_MR_SIZE_GB];
  uint32_t rkey[MAX_MR_SIZE_GB];
  int size_gb;
  //uint64_t size;
  enum {
    DONE = 1, //C
    INFO, //S
    INFO_SINGLE,
    FREE_SIZE, //S
    EVICT,
    ACTIVITY,
    STOP, //S
    BIND, //C
    BIND_SINGLE,
    QUERY //C
  } type;
};

struct context {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_comp_channel *comp_channel;

  pthread_t cq_poller_thread;
};

struct atomic_t{
  int value;
  pthread_mutex_t mutex;
};

struct connection {

  struct rdma_session *sess;
  int conn_index; //conn index in sess->conns
  int sess_chunk_map[MAX_MR_SIZE_GB];
  int mapped_chunk_size;

  sem_t evict_sem;
  sem_t stop_sem;

  struct rdma_cm_id *id;
  struct ibv_qp *qp;

  int connected;

  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
  struct ibv_mr *rdma_remote_mr;

  struct ibv_mr peer_mr;

  struct message *recv_msg;
  struct message *send_msg;

  char *rdma_remote_region;
  //struct rdma_remote_mem rdma_remote;

  struct atomic_t cq_qp_state;

  pthread_t free_mem_thread;
  long free_mem_gb;
  unsigned long rdma_buf_size;

  enum {
    S_WAIT,
    S_BIND,
    S_DONE
  } server_state;

  enum {
    SS_INIT,
    SS_MR_SENT, 
    SS_STOP_SENT,
    SS_DONE_SENT
  } send_state;

  enum {
    RS_INIT,
    RS_STOPPED_RECV,
    RS_DONE_RECV
  } recv_state;
};

#define CHUNK_MALLOCED 1
#define CHUNK_EMPTY	0
struct rdma_remote_mem{
  char* region_list[MAX_FREE_MEM_GB];
  struct ibv_mr* mr_list[MAX_FREE_MEM_GB]; 
  int size_gb; 
  int mapped_size;
  int conn_map[MAX_FREE_MEM_GB]; //chunk is used by which connection, or -1
  int malloc_map[MAX_FREE_MEM_GB];
  int conn_chunk_map[MAX_FREE_MEM_GB]; //session_chunk 
};

enum conn_state{
  CONN_IDLE,
  CONN_CONNECTED,
  CONN_MAPPED,
  CONN_FAILED
};

struct chunk_activity{
  uint64_t activity;
  int chunk_index;
};
struct rdma_session {
	struct connection* conns[MAX_CLIENT]; // need to init NULL
  enum conn_state conns_state[MAX_CLIENT];
	int conn_num;	

	struct rdma_remote_mem rdma_remote;		
  struct chunk_activity *evict_list;

};

void die(const char *reason);

void build_connection(struct rdma_cm_id *id);
void build_params(struct rdma_conn_param *params);
void destroy_connection(void *context);
void * get_serving_mem_region(void *context);
void on_connect(void *context);
void send_single_mr(void *context, int n);
void send_mr(void *context, int n);
void send_stop(void *context, int n);
void send_evict(void *context, int n);
void send_free_mem_size(void *context);
void* free_mem(void *data);

#endif
