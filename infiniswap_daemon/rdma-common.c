/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 * GPLv2 License
 */
#include "rdma-common.h"

extern long page_size;
extern int running;

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
static void post_receives(struct connection *conn);
static void register_memory(struct connection *conn);
static void send_message(struct connection *conn);

struct rdma_session session;

char free_mem_cmd[39] = "vmstat -s | awk 'FNR == 5 {printf $1}'";
static struct context *s_ctx = NULL;

void atomic_init(struct atomic_t *m)
{
  pthread_mutex_init(&m->mutex,NULL);
  m->value = -1;
}
void atomic_set(struct atomic_t *m, int val)
{
  pthread_mutex_lock(&m->mutex);
  m->value = val;
  pthread_mutex_unlock(&m->mutex);
}
int atomic_read(struct atomic_t *m)
{
  int res;
  pthread_mutex_lock(&m->mutex);
  res = m->value;
  pthread_mutex_unlock(&m->mutex);

  return res;
}

uint64_t htonll(uint64_t value)
{
     int num = 42;
     if(*(char *)&num == 42)
          return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32LL) | htonl(value >> 32);
     else 
          return value;
}

void die(const char *reason)
{
  fprintf(stderr, "%s\n", reason);
  exit(EXIT_FAILURE);
}

long get_free_mem(void)
{
  char result[60];
  FILE *fd = fopen("/proc/meminfo", "r");
  int i;
  long res = 0;
  fgets(result, 60, fd);
  memset(result, 0x00, 60);
  fgets(result, 60, fd);
  for (i=0;i<60;i++){
    if (result[i] >= 48 && result[i] <= 57){
      res *= 10;
      res += (int)(result[i] - 48);
    }
  }
  fclose(fd);
  return res;
}


void build_connection(struct rdma_cm_id *id)
{
  int i;
  struct connection *conn;
  struct ibv_qp_init_attr qp_attr;

  build_context(id->verbs);
  build_qp_attr(&qp_attr);

  TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));

  id->context = conn = (struct connection *)malloc(sizeof(struct connection));

  conn->id = id;
  conn->qp = id->qp;

  conn->send_state = SS_INIT;
  conn->recv_state = RS_INIT;
  conn->server_state = S_WAIT;

  conn->connected = 0;
  atomic_init(&conn->cq_qp_state);
  atomic_set(&conn->cq_qp_state, CQ_QP_BUSY);
  conn->free_mem_gb = 0;

  sem_init(&conn->stop_sem, 0, 0);
  sem_init(&conn->evict_sem, 0, 0);
  conn->sess = &session;
  for (i = 0; i < MAX_FREE_MEM_GB; i++){
    conn->sess_chunk_map[i] = -1;
  }
  conn->mapped_chunk_size = 0;
  //add to session 
  for (i=0; i<MAX_CLIENT; i++){
    if (session.conns_state[i] == CONN_IDLE){
      session.conns[i] = conn;
      session.conns_state[i] = CONN_CONNECTED;
      conn->conn_index = i;
      break;
    } 
  }
  session.conn_num += 1;

  register_memory(conn);
  post_receives(conn);
}

void build_context(struct ibv_context *verbs)
{
  if (s_ctx) {
    if (s_ctx->ctx != verbs)
      die("cannot handle events in more than one context.");

    return;
  }

  s_ctx = (struct context *)malloc(sizeof(struct context));

  s_ctx->ctx = verbs;

  TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
  TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
  TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
  TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));

  TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

void build_params(struct rdma_conn_param *params)
{
  memset(params, 0, sizeof(*params));

  params->initiator_depth = params->responder_resources = 1;
  params->rnr_retry_count = 7; /* infinite retry */
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
  memset(qp_attr, 0, sizeof(*qp_attr));

  qp_attr->send_cq = s_ctx->cq;
  qp_attr->recv_cq = s_ctx->cq;
  qp_attr->qp_type = IBV_QPT_RC;

  qp_attr->cap.max_send_wr = 10;
  qp_attr->cap.max_recv_wr = 100; //original 10
  qp_attr->cap.max_send_sge = 1;
  qp_attr->cap.max_recv_sge = 1;
}

void destroy_connection(void *context)
{
  struct connection *conn = (struct connection *)context;
  int i = 0;
  int index;
  rdma_destroy_qp(conn->id);

  ibv_dereg_mr(conn->send_mr);
  ibv_dereg_mr(conn->recv_mr);

  free(conn->send_msg);
  free(conn->recv_msg);

  if (conn->mapped_chunk_size > 0){
    for (i=0;i<MAX_MR_SIZE_GB; i++){
      index = conn->sess_chunk_map[i];
      if (index == -1) {
        continue;
      }
      session.rdma_remote.conn_map[index] = -1;
      session.rdma_remote.conn_chunk_map[index] = -1;
    } 
    session.rdma_remote.mapped_size -= conn->mapped_chunk_size; 
  }
  session.conns[conn->conn_index] = NULL;
  session.conns_state[conn->conn_index] = CONN_IDLE;
  session.conn_num -= 1;
  if (session.conn_num == 0){
    running = 0; 
  }
  rdma_destroy_id(conn->id);

  free(conn); 
}

void * get_serving_mem_region(void *context)
{
  return ((struct connection *)context)->rdma_remote_region;
}

void rdma_session_init(struct rdma_session *sess){
  int free_mem_g;
  int i;

  free_mem_g = (int)(get_free_mem() / ONE_MB);
  printf("%s, get free_mem %d\n", __func__, free_mem_g);
  for (i=0; i<MAX_FREE_MEM_GB; i++) {
    sess->rdma_remote.conn_map[i] = -1;
    sess->rdma_remote.conn_chunk_map[i] = -1;
    sess->rdma_remote.malloc_map[i] = CHUNK_EMPTY;
  }

  if (free_mem_g > FREE_MEM_EXPAND_THRESHOLD){
    free_mem_g -= (FREE_MEM_EVICT_THRESHOLD + FREE_MEM_EXPAND_THRESHOLD) / 2;
  } else if (free_mem_g > FREE_MEM_EVICT_THRESHOLD){
    free_mem_g  -= FREE_MEM_EVICT_THRESHOLD;
  }else{
    free_mem_g = 0;
  }
  if (free_mem_g > MAX_FREE_MEM_GB) {
    free_mem_g = MAX_FREE_MEM_GB;
  }

  for (i=0; i < free_mem_g; i++){
    posix_memalign((void **)&(sess->rdma_remote.region_list[i]), page_size, ONE_GB);
    memset(sess->rdma_remote.region_list[i], 0x00, ONE_GB);
    sess->rdma_remote.malloc_map[i] = CHUNK_MALLOCED;
  }
  sess->rdma_remote.size_gb = free_mem_g;
  sess->rdma_remote.mapped_size = 0;

  for (i=0; i<MAX_CLIENT; i++){
    sess->conns[i] = NULL;
    sess->conns_state[i] = CONN_IDLE;
  }
  sess->conn_num = 0;

  printf("%s, allocated mem %d\n", __func__, sess->rdma_remote.size_gb);

}

void evict_mem(int stop_g)
{
  int i, j, k, n, m;
  int freed_g = 0;
  int evict_g = stop_g;
  struct connection *conn;
  int avail_chunk;
  int random_chunk_select[MAX_FREE_MEM_GB];
  int send_list[MAX_CLIENT];
  unsigned int random_num;
  int conn_index;
  struct chunk_activity tmp_activity;
  int chunk_index;


  srand((unsigned)time(NULL));

  printf("need to evict %d GB\n", evict_g);
  //free unmapped chunk
  for (i = 0; i < MAX_FREE_MEM_GB ;i++) {
    if (session.rdma_remote.malloc_map[i] == CHUNK_MALLOCED && session.rdma_remote.conn_map[i] == -1){
      free(session.rdma_remote.region_list[i]);
      session.rdma_remote.malloc_map[i] = CHUNK_EMPTY;
      freed_g += 1;
      if (freed_g == evict_g){
        session.rdma_remote.size_gb -= evict_g;
        printf("free unmapped chunk %d\n", freed_g);
        return;
      }
    }
  }
  //not enough
  session.rdma_remote.size_gb -= freed_g;
  evict_g -= freed_g;

  //get availe_conn
  avail_chunk = MAX_FREE_MEM_GB;
  for (i=0; i<MAX_CLIENT; i++){
    // selection[i] = -1;
    send_list[i] = -1;
  }
  for (i = 0; i < MAX_FREE_MEM_GB; i++) {
    if (session.rdma_remote.conn_map[i] == -1) { // unmapped chunk
      avail_chunk -= 1;
      random_chunk_select[i] = -1; //can't select
    }else {
      random_chunk_select[i] = 0; //can select
    }
  }

  if (avail_chunk != session.rdma_remote.mapped_size){
    printf("%s, avail_chunk %d, mapped_size %d", __func__, avail_chunk, session.rdma_remote.mapped_size);
  }

  j = 0;  
  // evict_g += EXTRA_CHUNK_NUM;
  if (session.rdma_remote.mapped_size < (evict_g + EXTRA_CHUNK_NUM)){ //not enough
    if (session.rdma_remote.mapped_size < evict_g){
      evict_g = session.rdma_remote.mapped_size;
    }
    //send evict to all mapped cb
    for (i=0; i<MAX_CLIENT; i++){
      if (session.conns_state[i] == CONN_MAPPED){
        send_list[i] = 1; 
        j += session.conns[i]->mapped_chunk_size;
      }
    }  

    if (session.rdma_remote.mapped_size != j){
      printf("%s, error j %d, total mapped_size %d\n", __func__, j, session.rdma_remote.mapped_size);
    }
    j = evict_g;
  }else {
    printf(" mapped_size %d >= evict_g %d + EXTRA_CHUNK_NUM\n", session.rdma_remote.mapped_size, evict_g);
    for (j = 0; j < (evict_g + EXTRA_CHUNK_NUM); j++){
      random_num = rand() % MAX_FREE_MEM_GB;
      while (random_chunk_select[random_num] != 0){ //unmapped or selected
        random_num += 1;
        random_num %= MAX_FREE_MEM_GB;
      }
      random_chunk_select[random_num] = 1;
      send_list[session.rdma_remote.conn_map[random_num]] = 1; //send msg to this client
    } 
    j = evict_g;
    printf("evict_g %d\n", evict_g);
  } 
  printf("%s, selected chunk is %d\n", __func__, j);

  k = 0;
  for (i=0; i< MAX_CLIENT; i++){
    printf("i = %d ", i);
    if (send_list[i] == 1){
      k += session.conns[i]->mapped_chunk_size;
      printf("k is %d\n", k);
    }
  }
  printf("%s, total selected chunk is %d\n", __func__, k);
  session.evict_list = (struct chunk_activity *)malloc(sizeof(struct chunk_activity) * k);

  for (i=0; i< MAX_CLIENT; i++){
    if (send_list[i] == 1){
      printf("%s, send evict to conn[%d]\n", __func__, i);
      send_evict(session.conns[i], j);
    }
  }
  n = 0;
  for (i=0; i<MAX_CLIENT; i++){
    if (send_list[i] == 1){
      conn = session.conns[i];
      sem_wait(&conn->evict_sem);
      conn->send_msg->size_gb = 0;
      for (m=0; m<MAX_MR_SIZE_GB; m++){
        conn->send_msg->rkey[m] = 0;
        if (conn->recv_msg->buf[m]){
          session.evict_list[n].activity = ntohll(conn->recv_msg->buf[m]);
          session.evict_list[n].chunk_index = m;
          n += 1;
        }
      }
      post_receives(conn);
    }
  }
  if (n != k){
    printf("%s, received bitmap_info %d is not total_chunk %d\n", __func__, n, k);
  } 

  for (n=0; n < j; n++){//need evict chunk
    for (m=n+1; m < k; m++){ //total sorted chunk
      if (session.evict_list[n].activity > session.evict_list[m].activity){
        tmp_activity.activity = session.evict_list[n].activity; 
        tmp_activity.chunk_index = session.evict_list[n].chunk_index; 
        session.evict_list[n].activity = session.evict_list[m].activity;
        session.evict_list[n].chunk_index = session.evict_list[m].chunk_index;
        session.evict_list[m].activity = tmp_activity.activity;
        session.evict_list[m].chunk_index = tmp_activity.chunk_index;
      }
    }
    chunk_index = session.evict_list[n].chunk_index;
    conn_index = session.rdma_remote.conn_map[chunk_index];
    if (send_list[conn_index] == -1){
      printf("%s, send_list[%d] is -1 \n", __func__, conn_index);
    }
    if (send_list[conn_index] == 1){
      send_list[conn_index] = 2;
    }
    session.conns[conn_index]->send_msg->rkey[chunk_index] = 1;
    session.conns[conn_index]->send_msg->size_gb += 1;
  } 

  for (i=0; i<MAX_CLIENT; i++){
    if (send_list[i] == 2){
      send_stop(session.conns[i], session.conns[conn_index]->send_msg->size_gb);
    }else if (send_list[i] == 1){
      send_stop(session.conns[i], 0);
    }
  }
  free(session.evict_list);

}

void* free_mem(void *data)
{
  int free_mem_g = 0;
  int last_free_mem_g;
  int filtered_free_mem_g = 0;
  int evict_hit_count = 0;
  int expand_hit_count = 0;
  float last_free_mem_weight = 1 - CURR_FREE_MEM_WEIGHT;
  int stop_size_g;
  int expand_size_g;
  int i, j;

  rdma_session_init(&session);
  last_free_mem_g = (int)(get_free_mem() / ONE_MB);
  printf("%s, is called, last %d GB, weight: %f, %f\n", __func__, last_free_mem_g, (float)(CURR_FREE_MEM_WEIGHT), last_free_mem_weight); 

  while (running) {// server is working
    free_mem_g = (int)(get_free_mem() / ONE_MB);
    //need a filter
    filtered_free_mem_g = (int)(CURR_FREE_MEM_WEIGHT * free_mem_g + last_free_mem_g * last_free_mem_weight); 
    last_free_mem_g = filtered_free_mem_g;
    if (filtered_free_mem_g < FREE_MEM_EVICT_THRESHOLD){
      evict_hit_count += 1;
      expand_hit_count = 0;
      if (evict_hit_count >= MEM_EVICT_HIT_THRESHOLD){
        evict_hit_count = 0;
        //evict  down_threshold - free_mem
        stop_size_g = FREE_MEM_EVICT_THRESHOLD - last_free_mem_g;
        printf(", evict %d GB ", stop_size_g);
        if (session.rdma_remote.size_gb < stop_size_g){
          stop_size_g = session.rdma_remote.size_gb;
        }
        if (stop_size_g > 0){ //stop_size_g has to be meaningful.
          evict_mem(stop_size_g);
        }
        last_free_mem_g += stop_size_g;
      }
    }else if (filtered_free_mem_g > FREE_MEM_EXPAND_THRESHOLD) {
      expand_hit_count += 1;
      evict_hit_count = 0;
      if (expand_hit_count >= MEM_EXPAND_HIT_THRESHOLD){
        expand_hit_count = 0;
        expand_size_g =  last_free_mem_g - FREE_MEM_EXPAND_THRESHOLD;
        if ((expand_size_g + session.rdma_remote.size_gb) > MAX_FREE_MEM_GB) {
          expand_size_g = MAX_FREE_MEM_GB - session.rdma_remote.size_gb;
        }
        j = 0;
        for (i = 0; i < MAX_FREE_MEM_GB; i++){
          if (session.rdma_remote.malloc_map[i] == CHUNK_EMPTY){
            posix_memalign((void **)&(session.rdma_remote.region_list[i]), page_size, ONE_GB);
            memset(session.rdma_remote.region_list[i], 0x00, ONE_GB);
            session.rdma_remote.malloc_map[i] = CHUNK_MALLOCED;
            j += 1;
            if (j == expand_size_g){
              break;
            }
          }
        }
        session.rdma_remote.size_gb += expand_size_g;
        last_free_mem_g -= expand_size_g;
      }
    }
    // printf("\n"); 
    sleep(1); 
  }
  return NULL;
}

void recv_done(struct connection *conn)
{
  int evict_g = conn->recv_msg->size_gb;
  int i, j, index;
  
  j = 0;
  for (i = 0 ; i<MAX_MR_SIZE_GB; i++) {
    if (conn->recv_msg->rkey[i]){ //stopped this one
      j += 1;
      index = conn->sess_chunk_map[i];
      conn->sess_chunk_map[i] = -1;
      ibv_dereg_mr(session.rdma_remote.mr_list[index]);
      free(session.rdma_remote.region_list[index]);
      session.rdma_remote.conn_map[index] = -1;
      session.rdma_remote.malloc_map[index] = CHUNK_EMPTY;
      session.rdma_remote.conn_chunk_map[index] = -1;
    }  
  }
  if (j != evict_g) {
    printf("%s, error evict:%d, j:%d\n", __func__, evict_g, j);
  }
  session.rdma_remote.size_gb -= evict_g;
  session.rdma_remote.mapped_size -= evict_g;
  conn->mapped_chunk_size -= evict_g;

  sem_post(&conn->stop_sem);

  if (conn->mapped_chunk_size == 0) {
    //rdma_disconnect(conn->id); 
    session.conns_state[conn->conn_index]  = CONN_CONNECTED;
  }
  post_receives(conn); 
}

// Aug 21 handle queries from client
void on_completion(struct ibv_wc *wc)
{
  struct connection *conn = (struct connection *)(uintptr_t)wc->wr_id;

  if (wc->status != IBV_WC_SUCCESS)
    die("on_completion: status is not IBV_WC_SUCCESS.");

  if (wc->opcode == IBV_WC_RECV){ //Recv
    switch (conn->recv_msg->type){
      case QUERY:
        printf("%s, QUERY \n", __func__);
        atomic_set(&conn->cq_qp_state, CQ_QP_BUSY);
        send_free_mem_size(conn);
        post_receives(conn);
        break;
      case BIND:  //client bind with this server
        printf("%s, BIND \n", __func__);
        atomic_set(&conn->cq_qp_state, CQ_QP_BUSY);
        conn->server_state = S_BIND;
        //allocate n chunks, and send to client
        send_mr(conn, conn->recv_msg->size_gb);
        session.conns_state[conn->conn_index] = CONN_MAPPED;
        post_receives(conn);
        break;
      case BIND_SINGLE:
        printf("%s, BIND_SINGLE \n", __func__);
        atomic_set(&conn->cq_qp_state, CQ_QP_BUSY);
        conn->server_state = S_BIND;
        //allocate n chunks, and send to client
        send_single_mr(conn, conn->recv_msg->size_gb);
        session.conns_state[conn->conn_index] = CONN_MAPPED;
        post_receives(conn);
        break;
      case ACTIVITY:
        printf("%s, ACTIVITY \n", __func__);
        //copy bitmap data
        sem_post(&conn->evict_sem);
        break; 
      case DONE:
        printf("%s, DONE \n", __func__);
        atomic_set(&conn->cq_qp_state, CQ_QP_BUSY);
        recv_done(conn); //post receive inside
        break;
      default:
        die("unknow received message\n");
    }
  }else{ //Send
      atomic_set(&conn->cq_qp_state, CQ_QP_IDLE);
  }
}

void on_connect(void *context)
{
  struct connection *conn = (struct connection *)context;
  conn->connected = 1;
}



void * poll_cq(void *ctx)
{
  struct ibv_cq *cq;
  struct ibv_wc wc;

  while (1) {
    TEST_NZ(ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx));
    ibv_ack_cq_events(cq, 1);
    TEST_NZ(ibv_req_notify_cq(cq, 0));

    while (ibv_poll_cq(cq, 1, &wc))
      on_completion(&wc);
  }

  return NULL;
}

void post_receives(struct connection *conn)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)conn->recv_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->recv_mr->lkey;

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_memory(struct connection *conn)
{
  conn->send_msg = malloc(sizeof(struct message));
  conn->recv_msg = malloc(sizeof(struct message));

  TEST_Z(conn->send_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->send_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));

  TEST_Z(conn->recv_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->recv_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));

 }

void send_message(struct connection *conn)
{
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;

  sge.addr = (uintptr_t)conn->send_msg;
  sge.length = sizeof(struct message);
  printf("message size = %lu\n", sizeof(struct message));
  sge.lkey = conn->send_mr->lkey;

  while (!conn->connected);

  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}
void send_single_mr(void *context, int client_chunk_index)
{
  struct connection *conn = (struct connection *)context;
  int i = 0;

  conn->send_msg->size_gb = client_chunk_index;
  for (i=0; i<MAX_FREE_MEM_GB;i++){
    conn->send_msg->rkey[i] = 0;
  }
  for (i=0; i<MAX_FREE_MEM_GB; i++) {
    if (session.rdma_remote.malloc_map[i] == CHUNK_MALLOCED && session.rdma_remote.conn_map[i] == -1) {// allocated && unmapped 
      conn->sess_chunk_map[i] = i;
      session.rdma_remote.conn_map[i] = conn->conn_index;
      TEST_Z(session.rdma_remote.mr_list[i] = ibv_reg_mr(s_ctx->pd, session.rdma_remote.region_list[i], ONE_GB, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ)); //Write permission can't cover read permission, different traditional understanding
      conn->send_msg->buf[i] = htonll((uint64_t)session.rdma_remote.mr_list[i]->addr);
      conn->send_msg->rkey[i] = htonl((uint64_t)session.rdma_remote.mr_list[i]->rkey);
      printf("RDMA addr %llx  rkey %x\n", (unsigned long long)conn->send_msg->buf[i], conn->send_msg->rkey[i]);
      break;
    }
  } 
  session.rdma_remote.mapped_size += 1;
  conn->mapped_chunk_size += 1;
  conn->send_msg->type = INFO_SINGLE;

  send_message(conn);
}
void send_mr(void *context, int size)
{
  struct connection *conn = (struct connection *)context;
  int i = 0;
  int j = 0;

  conn->send_msg->size_gb = size;
  for (i=0; i<MAX_FREE_MEM_GB;i++){
    conn->send_msg->rkey[i] = 0;
  }
  for (i=0; i<MAX_FREE_MEM_GB; i++) {
    if (session.rdma_remote.malloc_map[i] == CHUNK_MALLOCED && session.rdma_remote.conn_map[i] == -1) {// allocated && unmapped 
      conn->sess_chunk_map[i] = i;
      session.rdma_remote.conn_map[i] = conn->conn_index;
      TEST_Z(session.rdma_remote.mr_list[i] = ibv_reg_mr(s_ctx->pd, session.rdma_remote.region_list[i], ONE_GB, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ)); //Write permission can't cover read permission, different traditional understanding
      conn->send_msg->buf[i] = htonll((uint64_t)session.rdma_remote.mr_list[i]->addr);
      conn->send_msg->rkey[i] = htonl((uint64_t)session.rdma_remote.mr_list[i]->rkey);
      printf("RDMA addr %llx  rkey %x\n", (unsigned long long)conn->send_msg->buf[i], conn->send_msg->rkey[i]);
      j += 1;
      if (j == size){
        break;
      }
    }
  } 
  session.rdma_remote.mapped_size += size;
  conn->mapped_chunk_size += size;
  conn->send_msg->type = INFO;

  send_message(conn);
}

void send_free_mem_size(void *context)
{
  struct connection *conn = (struct connection *)context;

  conn->send_msg->type = FREE_SIZE;
  conn->send_msg->size_gb = session.rdma_remote.size_gb - session.rdma_remote.mapped_size;
  printf("%s , %d\n", __func__, conn->send_msg->size_gb);
  send_message(conn);
}

//stop n chunks
void send_stop(void *context, int n)
{
  struct connection *conn = (struct connection *)context;
  printf("%s, stop %d GB in %d conn\n", __func__, n, conn->conn_index);
  conn->send_msg->type = STOP;
  conn->send_msg->size_gb = n;
  send_message(conn);
  if (n != 0){ //0 means no need to stop, just info client
   sem_wait(&conn->stop_sem);
  }
}
void send_evict(void *context, int n)
{
  int i;
  struct connection *conn = (struct connection *)context;
  printf("%s, EVICT %d GB in conn%d\n", __func__, n, conn->conn_index);
  conn->send_msg->type = EVICT;
  conn->send_msg->size_gb = n;
  for (i=0; i<MAX_MR_SIZE_GB; i++){
    conn->send_msg->rkey[i] = 0;
  }
  send_message(conn);
}

void  send_terminate(void *context)
{
  struct connection *conn = (struct connection *)context;
  conn->send_msg->type = DONE;
  send_message(conn);
}