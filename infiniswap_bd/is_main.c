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

#include "infiniswap.h"

#define DRV_NAME	"IS"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"0.0"

MODULE_AUTHOR("Juncheng Gu, Youngmoon Lee, from Sagi Grimberg, Max Gurtovoy");
MODULE_DESCRIPTION("Infiniswap, remote memory paging over RDMA");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

int created_portals = 0;
int IS_major;
int IS_indexes; /* num of devices created*/
int submit_queues; // num of available cpu (also connections)
struct list_head g_IS_sessions;
struct mutex g_lock;
int NUM_CB;	// num of server/cb

inline int IS_set_device_state(struct IS_file *xdev,
				 enum IS_dev_state state)
{
	int ret = 0;

	spin_lock(&xdev->state_lock);
	switch (state) {
	case DEVICE_OPENNING:
		if (xdev->state == DEVICE_OFFLINE ||
		    xdev->state == DEVICE_RUNNING) {
			ret = -EINVAL;
			goto out;
		}
		xdev->state = state;
		break;
	case DEVICE_RUNNING:
		xdev->state = state;
		break;
	case DEVICE_OFFLINE:
		xdev->state = state;
		break;
	default:
		pr_err("Unknown device state %d\n", state);
		ret = -EINVAL;
	}
out:
	spin_unlock(&xdev->state_lock);
	return ret;
}

static struct rdma_ctx *IS_get_ctx(struct ctx_pool_list *tmp_pool)
{
	struct free_ctx_pool *free_ctxs = tmp_pool->free_ctxs;
	struct rdma_ctx *res;
	unsigned long flags;

	spin_lock_irqsave(&free_ctxs->ctx_lock, flags);

	if (free_ctxs->tail == -1){
		spin_unlock_irqrestore(&free_ctxs->ctx_lock, flags);
		return NULL;
	}
	res = free_ctxs->ctx_list[free_ctxs->tail];
	free_ctxs->tail = free_ctxs->tail - 1;
	
	spin_unlock_irqrestore(&free_ctxs->ctx_lock, flags);

	return res;
}

void IS_insert_ctx(struct rdma_ctx *ctx)
{
	struct free_ctx_pool *free_ctxs = ctx->free_ctxs;
	unsigned long flags;

	spin_lock_irqsave(&free_ctxs->ctx_lock, flags);

	free_ctxs->tail = free_ctxs->tail + 1;
	free_ctxs->ctx_list[free_ctxs->tail] = ctx;
	if (free_ctxs->tail > IS_QUEUE_DEPTH - 1){
		pr_err("%s, tail = %d\n", __func__, free_ctxs->tail);
	}

	spin_unlock_irqrestore(&free_ctxs->ctx_lock, flags);
}

int IS_rdma_read(struct IS_connection *IS_conn, struct kernel_cb *cb, int cb_index, int chunk_index, struct remote_chunk_g *chunk, unsigned long offset, unsigned long len, struct request *req, struct IS_queue *q)
{
	int ret;
	struct ib_send_wr *bad_wr;
	struct rdma_ctx *ctx = NULL;
	int ctx_loop = 0;
	
	// get ctx_buf based on request address
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	int conn_id = (uint64_t)( bio_data(req->bio)   ) & QUEUE_NUM_MASK;
	#else
	int conn_id = (uint64_t)(req->buffer) & QUEUE_NUM_MASK;
	#endif

	IS_conn = IS_conn->IS_sess->IS_conns[conn_id];
	ctx = IS_get_ctx(IS_conn->ctx_pools[cb_index]);
	while (!ctx){
		if ( (++ctx_loop) == submit_queues){
			ctx_loop = 0;	
			msleep(1);
		}
		conn_id = (conn_id + 1) % submit_queues;	
		IS_conn = IS_conn->IS_sess->IS_conns[conn_id];
		ctx = IS_get_ctx(IS_conn->ctx_pools[cb_index]);
	}

	ctx->req = req;
	ctx->chunk_index = chunk_index; //chunk_index in cb
	atomic_set(&ctx->in_flight, CTX_R_IN_FLIGHT);  
	if (atomic_read(&IS_conn->IS_sess->rdma_on) != DEV_RDMA_ON){	
		pr_info("%s, rdma_off, go to disk\n", __func__);
		atomic_set(&ctx->in_flight, CTX_IDLE);  
		IS_insert_ctx(ctx);
		IS_mq_request_stackbd2(req);
		return 0;
	}

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	ctx->rdma_sq_wr.wr.sg_list->length = len;
	ctx->rdma_sq_wr.rkey = chunk->remote_rkey;
	ctx->rdma_sq_wr.remote_addr = chunk->remote_addr + offset;
	ctx->rdma_sq_wr.wr.opcode = IB_WR_RDMA_READ;
	#else
	ctx->rdma_sq_wr.sg_list->length = len;
	ctx->rdma_sq_wr.wr.rdma.rkey = chunk->remote_rkey;
	ctx->rdma_sq_wr.wr.rdma.remote_addr = chunk->remote_addr + offset;
	ctx->rdma_sq_wr.opcode = IB_WR_RDMA_READ;
	#endif	
	ret = ib_post_send(cb->qp, (struct ib_send_wr *) &ctx->rdma_sq_wr, &bad_wr);

	if (ret) {
		printk(KERN_ALERT PFX "client post read %d, wr=%p\n", ret, &ctx->rdma_sq_wr);
		return ret;
	}	
	return 0;
}

void stackbd_bio_generate(struct rdma_ctx *ctx, struct request *req)
{
	struct bio *cloned_bio = NULL;
	struct page *pg = NULL;
	unsigned int nr_segs = req->nr_phys_segments;
	unsigned int io_size = nr_segs * IS_PAGE_SIZE;

	cloned_bio = bio_clone(req->bio, GFP_ATOMIC); 
	pg = virt_to_page(ctx->rdma_buf);
	cloned_bio->bi_io_vec->bv_page  = pg; 
	cloned_bio->bi_io_vec->bv_len = io_size;
	cloned_bio->bi_io_vec->bv_offset = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	cloned_bio->bi_iter.bi_size = io_size;
#else
	cloned_bio->bi_size = io_size;
#endif
	cloned_bio->bi_private = uint64_from_ptr(ctx);
	stackbd_make_request5(cloned_bio);
}

void mem_gather(char *rdma_buf, struct request *req)
{
	char *buffer = NULL;
	unsigned int i = 0;
	unsigned int j = 0;
	struct bio *tmp = req->bio;
	unsigned int nr_seg = req->nr_phys_segments;

	for (i=0; i < nr_seg;){
		buffer = bio_data(tmp);
		j = tmp->bi_phys_segments;
		memcpy(rdma_buf + (i * IS_PAGE_SIZE), buffer, IS_PAGE_SIZE * j);
		i += j;
		tmp = tmp->bi_next;
	}
}

int IS_rdma_write(struct IS_connection *IS_conn, struct kernel_cb *cb, int cb_index, int chunk_index, struct remote_chunk_g *chunk, unsigned long offset, unsigned long len, struct request *req, struct IS_queue *q)
{
	int ret;
	struct ib_send_wr *bad_wr;	
	struct rdma_ctx *ctx;
	int ctx_loop = 0;

	// get ctx_buf based on request address
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	int conn_id = (uint64_t)(bio_data(req->bio)) & QUEUE_NUM_MASK;
#else
	int conn_id = (uint64_t)(req->buffer) & QUEUE_NUM_MASK;
#endif
	IS_conn = IS_conn->IS_sess->IS_conns[conn_id];
	ctx = IS_get_ctx(IS_conn->ctx_pools[cb_index]);
	while (!ctx){
		if ( (++ctx_loop) == submit_queues){
			ctx_loop = 0;	
			msleep(1);
		}
		conn_id = (conn_id + 1) % submit_queues;	
		IS_conn = IS_conn->IS_sess->IS_conns[conn_id];
		ctx = IS_get_ctx(IS_conn->ctx_pools[cb_index]);
	}

	ctx->req = req;
	ctx->cb = cb;
	ctx->offset = offset;
	ctx->len = len;
	ctx->chunk_ptr = chunk;
	ctx->chunk_index = chunk_index;

	atomic_set(&ctx->in_flight, CTX_W_IN_FLIGHT);
	if (atomic_read(&IS_conn->IS_sess->rdma_on) != DEV_RDMA_ON){	
		pr_info("%s, rdma_off, give up the write request\n", __func__);
		atomic_set(&ctx->in_flight, CTX_IDLE);
		IS_insert_ctx(ctx);
		IS_mq_request_stackbd2(req);
	
		return 0;
	}

	mem_gather(ctx->rdma_buf, req);
	stackbd_bio_generate(ctx, req);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	ctx->rdma_sq_wr.wr.sg_list->length = len;
	ctx->rdma_sq_wr.rkey = chunk->remote_rkey;
	ctx->rdma_sq_wr.remote_addr = chunk->remote_addr + offset;
	ctx->rdma_sq_wr.wr.opcode = IB_WR_RDMA_WRITE;
#else
	ctx->rdma_sq_wr.sg_list->length = len;
	ctx->rdma_sq_wr.wr.rdma.rkey = chunk->remote_rkey;
	ctx->rdma_sq_wr.wr.rdma.remote_addr = chunk->remote_addr + offset;
	ctx->rdma_sq_wr.opcode = IB_WR_RDMA_WRITE;
#endif
	ret = ib_post_send(cb->qp, (struct ib_send_wr *) &ctx->rdma_sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ALERT PFX "client post write %d, wr=%p\n", ret, &ctx->rdma_sq_wr);
		return ret;
	}
	return 0;
}

uint32_t bitmap_value(int *bitmap)
{
	int i;
	uint32_t val = 1;
	for (i =0; i < BITMAP_INT_SIZE; i+=32) {
		if (bitmap[i] != 0){
			val += 1;	
		}
	}	
	return val;
}
static int IS_send_activity(struct kernel_cb *cb)
{
	int ret = 0;
	struct ib_send_wr *bad_wr;	
	int i;
	int count=0;
	int chunk_sess_index = -1;
	struct IS_session *IS_sess = cb->IS_sess;
	cb->send_buf.type = ACTIVITY;

	for (i=0; i<MAX_MR_SIZE_GB; i++) {
		chunk_sess_index = cb->remote_chunk.chunk_map[i];
		if (chunk_sess_index != -1){ //mapped chunk
			cb->send_buf.buf[i] = htonll((IS_sess->last_ops[chunk_sess_index] + 1));
			count += 1;
		}else { //unmapped chunk
			cb->send_buf.buf[i] = 0;	
		}
	}
	ret = ib_post_send(cb->qp,  &cb->sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR PFX "ACTIVITY MSG send error %d\n", ret);
		return ret;
	}
	return 0;
}

static int IS_send_query(struct kernel_cb *cb)
{
	int ret = 0;
	struct ib_send_wr * bad_wr;

	cb->send_buf.type = QUERY;
	ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR PFX "QUERY MSG send error %d\n", ret);
		return ret;
	}
	return 0;
}
static int IS_send_bind_single(struct kernel_cb *cb, int select_chunk)
{
	int ret = 0;
	struct ib_send_wr * bad_wr;
	cb->send_buf.type = BIND_SINGLE;
	cb->send_buf.size_gb = select_chunk; 

	ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR PFX "BIND_SINGLE MSG send error %d\n", ret);
		return ret;
	}
	return 0;	
}

static int IS_send_done(struct kernel_cb *cb, int num)
{
	int ret = 0;
	struct ib_send_wr * bad_wr;
	cb->send_buf.type = DONE;
	cb->send_buf.size_gb = num;
	ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR PFX "DONE MSG send error %d\n", ret);
		return ret;
	}
	return 0;
}

int IS_transfer_chunk(struct IS_file *xdev, struct kernel_cb *cb, int cb_index, int chunk_index, struct remote_chunk_g *chunk, unsigned long offset,
		  unsigned long len, int write, struct request *req,
		  struct IS_queue *q)
{
	struct IS_connection *IS_conn = q->IS_conn;
	int cpu, retval = 0;

	cpu = get_cpu();
	

	if (write){
		retval = IS_rdma_write(IS_conn, cb, cb_index, chunk_index, chunk, offset, len, req, q); 
		if (unlikely(retval)) {
			pr_err("failed to map sg\n");
			goto err;
		}
	}else{
		retval = IS_rdma_read(IS_conn, cb, cb_index, chunk_index, chunk, offset, len, req, q); 
		if (unlikely(retval)) {
			pr_err("failed to map sg\n");
			goto err;
		}
	}
	put_cpu();
	return 0;
err:
	return retval;
}

// mainly used to confirm that this device is not created; called before create device
struct IS_file *IS_file_find(struct IS_session *IS_session,
				 const char *xdev_name)
{
	struct IS_file *pos;
	struct IS_file *ret = NULL;

	spin_lock(&IS_session->devs_lock);
	list_for_each_entry(pos, &IS_session->devs_list, list) {
		if (!strcmp(pos->file_name, xdev_name)) {
			ret = pos;
			break;
		}
	}
	spin_unlock(&IS_session->devs_lock);

	return ret;
}

// confirm that this portal (remote server port) is not used; called before create session
struct IS_session *IS_session_find_by_portal(struct list_head *s_data_list,
						 const char *portal)
{
	struct IS_session *pos;
	struct IS_session *ret = NULL;

	mutex_lock(&g_lock);
	list_for_each_entry(pos, s_data_list, list) {
		if (!strcmp(pos->portal, portal)) {
			ret = pos;
			break;
		}
	}
	mutex_unlock(&g_lock);

	return ret;
}

static int IS_disconnect_handler(struct kernel_cb *cb)
{
	int pool_index = cb->cb_index;
	int i, j=0;
	struct rdma_ctx *ctx_pool;
	struct rdma_ctx *ctx;
	struct IS_session *IS_sess = cb->IS_sess;
	int *cb_chunk_map = cb->remote_chunk.chunk_map;
	int sess_chunk_index;
	int err = 0;
	int evict_list[STACKBD_SIZE_G];
	struct request *req;

	pr_debug("%s\n", __func__);

	for (i=0; i<STACKBD_SIZE_G;i++){
		evict_list[i] = -1;
	}

	// for connected, but not mapped server
	if (IS_sess->cb_state_list[cb->cb_index] == CB_CONNECTED){
		pr_info("%s, connected_cb [%d] is disconnected\n", __func__, cb->cb_index);
		//need to clean cb info/struct
		IS_sess->cb_state_list[cb->cb_index] = CB_FAIL;
		return cb->cb_index;
	}

	//change cb state
	IS_sess->cb_state_list[cb->cb_index] = CB_FAIL;
	atomic_set(&IS_sess->trigger_enable, TRIGGER_OFF);
	atomic_set(&cb->IS_sess->rdma_on, DEV_RDMA_OFF);

	//disallow request to those cb chunks 
	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		sess_chunk_index = cb_chunk_map[i];
		if (sess_chunk_index != -1) { //this cb chunk is mapped
			evict_list[sess_chunk_index] = 1;
			IS_bitmap_init(cb->remote_chunk.chunk_list[i]->bitmap_g); //should be in in_flight_thread
			atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_UNMAPPED);
			atomic_set(IS_sess->cb_index_map + (sess_chunk_index), NO_CB_MAPPED); 
			pr_debug("%s, unmap chunk %d\n", __func__, sess_chunk_index);
		}
	}	

	pr_debug("%s, unmap %d GB in cb%d \n", __func__, cb->remote_chunk.chunk_size_g, pool_index);
	cb->remote_chunk.chunk_size_g = 0;

	msleep(10);

	for (i=0; i < submit_queues; i++){
		ctx_pool = IS_sess->IS_conns[i]->ctx_pools[pool_index]->ctx_pool;
		for (j=0; j < IS_QUEUE_DEPTH; j++){
			ctx = ctx_pool + j;
			switch (atomic_read(&ctx->in_flight)){
				case CTX_R_IN_FLIGHT:
					req = ctx->req;
					atomic_set(&ctx->in_flight, CTX_IDLE);
					IS_mq_request_stackbd2(req);
					IS_insert_ctx(ctx);
					break;
				case CTX_W_IN_FLIGHT:
					atomic_set(&ctx->in_flight, CTX_IDLE);
					if (ctx->req == NULL){ 
						break;
					}
				#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
					blk_mq_end_request(ctx->req, 0);
				#else
					blk_mq_end_io(ctx->req, 0);
				#endif
					break;
				default:
					;
			}
		}
	}	
	pr_err("%s, finish handling in-flight request\n", __func__);

	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		sess_chunk_index = cb_chunk_map[i];
		if (sess_chunk_index != -1) { 
			IS_sess->chunk_map_cb_chunk[sess_chunk_index] = -1;
			IS_sess->free_chunk_index += 1;
			IS_sess->unmapped_chunk_list[IS_sess->free_chunk_index] = sess_chunk_index;
			cb_chunk_map[i] = -1;
		}
	}

	//free conn->ctx_pools[cb_index]
	for (i =0; i<submit_queues; i++){
		kfree(IS_sess->IS_conns[i]->ctx_pools[pool_index]->ctx_pool);
		kfree(IS_sess->IS_conns[i]->ctx_pools[pool_index]->free_ctxs->ctx_list);
		kfree(IS_sess->IS_conns[i]->ctx_pools[pool_index]->free_ctxs);
		kfree(IS_sess->IS_conns[i]->ctx_pools[pool_index]);
		IS_sess->IS_conns[i]->ctx_pools[pool_index] = (struct ctx_pool_list *)kzalloc(sizeof(struct ctx_pool_list), GFP_KERNEL);
	}

	atomic_set(&cb->IS_sess->rdma_on, DEV_RDMA_ON);
	for (i=0; i<STACKBD_SIZE_G; i++){
		if (evict_list[i] == 1){
			IS_single_chunk_map(IS_sess, i);
		}
	}

	atomic_set(&IS_sess->trigger_enable, TRIGGER_ON);

	pr_err("%s, exit\n", __func__);
	return err;
}

static int IS_cma_event_handler(struct rdma_cm_id *cma_id,
				   struct rdma_cm_event *event)
{
	int ret;
	struct kernel_cb *cb = cma_id->context;

	pr_info("cma_event type %d cma_id %p (%s)\n", event->event, cma_id,
		  (cma_id == cb->cm_id) ? "parent" : "child");

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		cb->state = ADDR_RESOLVED;
		ret = rdma_resolve_route(cma_id, 2000);
		if (ret) {
			printk(KERN_ERR PFX "rdma_resolve_route error %d\n", 
			       ret);
			wake_up_interruptible(&cb->sem);
		}
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		cb->state = ROUTE_RESOLVED;
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		cb->state = CONNECT_REQUEST;
		cb->child_cm_id = cma_id;
		pr_info("child cma %p\n", cb->child_cm_id);
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		pr_info("ESTABLISHED\n");
		cb->state = CONNECTED;
		wake_up_interruptible(&cb->sem);
		// last connection establish will wake up the IS_session_create()
		if (atomic_dec_and_test(&cb->IS_sess->conns_count)) {
			pr_debug("%s: last connection established\n", __func__);
			complete(&cb->IS_sess->conns_wait);
		}
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		printk(KERN_ERR PFX "cma event %d, error %d\n", event->event,
		       event->status);
		cb->state = ERROR;
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:	//should get error msg from here
		printk(KERN_ERR PFX "DISCONNECT EVENT...\n");
		cb->state = CM_DISCONNECT;
		// RDMA is off
		IS_disconnect_handler(cb);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:	//this also should be treated as disconnection, and continue disk swap
		printk(KERN_ERR PFX "cma detected device removal!!!!\n");
		return -1;
		break;

	default:
		printk(KERN_ERR PFX "oof bad type!\n");
		wake_up_interruptible(&cb->sem);
		break;
	}
	return 0;
}

static int IS_chunk_wait_in_flight_requests(struct kernel_cb *cb)
{
	int pool_index = cb->cb_index;
	int i, j=0;
	struct rdma_ctx *ctx_pool;
	struct rdma_ctx *ctx;
	struct IS_session *IS_sess = cb->IS_sess;
	int *chunk_map = cb->remote_chunk.chunk_map;
	int err = 0;

	msleep(1);
	while (1) {
		for (i=0; i < submit_queues; i++){
			ctx_pool = IS_sess->IS_conns[i]->ctx_pools[pool_index]->ctx_pool;
			for (j=0; j < IS_QUEUE_DEPTH; j++){
				ctx = ctx_pool + j;
				switch (atomic_read(&ctx->in_flight)){
					case CTX_R_IN_FLIGHT:
					case CTX_W_IN_FLIGHT:
						//the chunk is going to be cancelled
						pr_debug("%s %d %d in write flight %p start 0x%lx, chunk_index %d\n", __func__, i, j, ctx->req, (blk_rq_pos(ctx->req) << IS_SECT_SHIFT), ctx->chunk_index);
						if (chunk_map[ctx->chunk_index] == -1){
							err = 1;
						}
						break;
					default:
						;
				}
				if (err)
					break;
			}
			if (err)
				break;
		}	
		if (i == submit_queues && j == IS_QUEUE_DEPTH){
			break;
		}else{
			err = 0;
			msleep(10);
		}
	}
	return err; 
}

static int evict_handler(void *data)
{
	struct kernel_cb *cb = data;	
	int size_g;
	int i;
	int j;
	int err = 0;
	int sess_chunk_index;
	int *cb_chunk_map = cb->remote_chunk.chunk_map;
	struct IS_session *IS_sess = cb->IS_sess;
	int evict_list[STACKBD_SIZE_G]; //session chunk index

	while (cb->state != ERROR) {
		pr_err("%s, waiting for STOP msg\n", __func__);
		wait_event_interruptible(cb->remote_chunk.sem, (cb->remote_chunk.c_state == C_EVICT));	
		size_g = cb->remote_chunk.shrink_size_g;

		IS_send_activity(cb);
		wait_event_interruptible(cb->remote_chunk.sem, (cb->remote_chunk.c_state == C_STOP));	
		size_g = cb->remote_chunk.shrink_size_g;
		if (size_g == 0){
			cb->remote_chunk.c_state = C_READY;
			continue;
		}
		for (i=0; i<STACKBD_SIZE_G; i++){
			evict_list[i] = -1;	
		}
		for (i = 0; i < MAX_MR_SIZE_GB; i++) {
			cb->send_buf.rkey[i] = 0;
		}
		j = 0;

		atomic_set(&IS_sess->trigger_enable, TRIGGER_OFF);
		for (i = 0; i < MAX_MR_SIZE_GB; i++) {
			if (cb->remote_chunk.evict_chunk_map[i] == 's'){ // need to stop this chunk
				sess_chunk_index = cb_chunk_map[i];
				atomic_set(IS_sess->cb_index_map + (sess_chunk_index), NO_CB_MAPPED); 
				evict_list[sess_chunk_index] = 1;
				cb_chunk_map[i] = -1;
				cb->send_buf.rkey[i] = 1; //tag this chunk should be removed
				j += 1;
			}else{
				cb->send_buf.rkey[i] = 0;
			}
		}

		IS_chunk_wait_in_flight_requests(cb);
		for (i = 0; i < MAX_MR_SIZE_GB; i++) {
			if (cb->remote_chunk.evict_chunk_map[i] == 's'){ // need to stop this chunk
				IS_bitmap_init(cb->remote_chunk.chunk_list[i]->bitmap_g); 
				atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_UNMAPPED);
			}
		}
		
		IS_sess->mapped_cb_num -= size_g;
		cb->remote_chunk.chunk_size_g -= size_g;
		cb->remote_chunk.shrink_size_g = 0;
		IS_send_done(cb, size_g);	

		cb->remote_chunk.c_state = C_READY;
		IS_sess->cb_state_list[cb->cb_index] = CB_EVICTING;
		for (i=0; i<STACKBD_SIZE_G; i++){
			if (evict_list[i] == 1){
				IS_sess->chunk_map_cb_chunk[i] = -1;
				IS_sess->free_chunk_index += 1;
				IS_sess->unmapped_chunk_list[IS_sess->free_chunk_index] = i;
				IS_single_chunk_map(IS_sess, i);
			}
		}	
		IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
		atomic_set(&IS_sess->trigger_enable, TRIGGER_ON);
	}
	return err;
}

static void client_recv_evict(struct kernel_cb *cb) 
{
	if (cb->recv_buf.size_gb == 0){
		return;
	}
	cb->remote_chunk.shrink_size_g = cb->recv_buf.size_gb;	
	cb->remote_chunk.c_state = C_EVICT;
	wake_up_interruptible(&cb->remote_chunk.sem);
}
static void client_recv_stop(struct kernel_cb *cb)
{
	int i;
	int count = 0;
	cb->remote_chunk.shrink_size_g = cb->recv_buf.size_gb;
	if (cb->recv_buf.size_gb == 0){
		pr_err("%s, doesn't have to evict\n", __func__);
		cb->remote_chunk.c_state = C_STOP;
		wake_up_interruptible(&cb->remote_chunk.sem);
		return;
	}
	for (i=0; i<MAX_MR_SIZE_GB; i++){
		if (cb->recv_buf.rkey[i]){
			cb->remote_chunk.evict_chunk_map[i] = 's'; // need to stop
			count += 1;
		}else{
			cb->remote_chunk.evict_chunk_map[i] = 'a'; // not related
		}
	}
	cb->remote_chunk.c_state = C_STOP;
	wake_up_interruptible(&cb->remote_chunk.sem);
}

static int client_recv(struct kernel_cb *cb, struct ib_wc *wc)
{
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		printk(KERN_ERR PFX "Received bogus data, size %d\n", 
		       wc->byte_len);
		return -1;
	}	
	if (cb->state < CONNECTED){
		printk(KERN_ERR PFX "cb is not connected\n");	
		return -1;
	}
	switch(cb->recv_buf.type){
		case FREE_SIZE:
			cb->remote_chunk.target_size_g = cb->recv_buf.size_gb;
			cb->state = FREE_MEM_RECV;	
			break;
		case INFO:
			cb->IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
			cb->state = WAIT_OPS;
			IS_chunk_list_init(cb);
			break;
		case INFO_SINGLE:
			cb->IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
			cb->state = WAIT_OPS;
			IS_single_chunk_init(cb);
			break;
		case EVICT:
			cb->state = RECV_EVICT;
			client_recv_evict(cb);
			break;
		case STOP:
			cb->state = RECV_STOP;	
			client_recv_stop(cb);
			break;
		default:
			pr_info(PFX "client receives unknown msg\n");
			return -1; 	
	}
	return 0;
}

static int client_send(struct kernel_cb *cb, struct ib_wc *wc)
{
	return 0;	
}

static int client_read_done(struct kernel_cb * cb, struct ib_wc *wc)
{
	struct rdma_ctx *ctx;
	struct request *req;

	ctx = (struct rdma_ctx *)ptr_from_uint64(wc->wr_id);
	atomic_set(&ctx->in_flight, CTX_IDLE);
	ctx->chunk_index = -1;
	req = ctx->req;
	ctx->req = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	memcpy(bio_data(req->bio), ctx->rdma_buf, IS_PAGE_SIZE);
#else
	memcpy(req->buffer, ctx->rdma_buf, IS_PAGE_SIZE);
#endif

	IS_insert_ctx(ctx); 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	blk_mq_end_request(req, 0);
#else
	blk_mq_end_io(req, 0);
#endif		
	return 0;
}

static int client_write_done(struct kernel_cb * cb, struct ib_wc *wc)
{
	struct rdma_ctx *ctx=NULL;
	struct request *req=NULL;

	ctx = (struct rdma_ctx *)ptr_from_uint64(wc->wr_id);	
	if (ctx->chunk_ptr == NULL){
		return 0;
	}

	atomic_set(&ctx->in_flight, CTX_IDLE);
	IS_bitmap_group_set(ctx->chunk_ptr->bitmap_g, ctx->offset, ctx->len);
	ctx->chunk_index = -1;
	ctx->chunk_ptr = NULL;
	if (ctx->req == NULL){ 
		return 0;
	}
	req = ctx->req;
	ctx->req = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	blk_mq_end_request(req, 0);
#else
	blk_mq_end_io(req, 0);
#endif		
	return 0;
}

static void rdma_cq_event_handler(struct ib_cq * cq, void *ctx)
{
	struct kernel_cb *cb=ctx;
	struct ib_wc wc;
	struct ib_recv_wr * bad_wr;
	int ret;
	BUG_ON(cb->cq != cq);
	if (cb->state == ERROR) {
		printk(KERN_ERR PFX "cq completion in ERROR state\n");
		return;
	}
	ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);

	while ((ret = ib_poll_cq(cb->cq, 1, &wc)) == 1) {
		if (wc.status) {
			if (wc.status == IB_WC_WR_FLUSH_ERR) {
				pr_info("cq flushed\n");
				continue;
			} else {
				printk(KERN_ERR PFX "cq completion failed with "
				       "wr_id %Lx status %d opcode %d vender_err %x\n",
					wc.wr_id, wc.status, wc.opcode, wc.vendor_err);
				goto error;
			}
		}	
		switch (wc.opcode){
			case IB_WC_RECV:
				ret = client_recv(cb, &wc);
				if (ret) {
					printk(KERN_ERR PFX "recv wc error: %d\n", ret);
					goto error;
				}

				ret = ib_post_recv(cb->qp, &cb->rq_wr, &bad_wr);
				if (ret) {
					printk(KERN_ERR PFX "post recv error: %d\n", 
					       ret);
					goto error;
				}
				if (cb->state == RDMA_BUF_ADV || cb->state == FREE_MEM_RECV || cb->state == WAIT_OPS){
					wake_up_interruptible(&cb->sem);
				}
				break;
			case IB_WC_SEND:
				ret = client_send(cb, &wc);
				if (ret) {
					printk(KERN_ERR PFX "send wc error: %d\n", ret);
					goto error;
				}
				break;
			case IB_WC_RDMA_READ:
				ret = client_read_done(cb, &wc);
				if (ret) {
					printk(KERN_ERR PFX "read wc error: %d, cb->state=%d\n", ret, cb->state);
					goto error;
				}
				break;
			case IB_WC_RDMA_WRITE:
				ret = client_write_done(cb, &wc);
				if (ret) {
					printk(KERN_ERR PFX "write wc error: %d, cb->state=%d\n", ret, cb->state);
					goto error;
				}
				break;
			default:
				printk(KERN_ERR PFX "%s:%d Unexpected opcode %d, Shutting down\n", __func__, __LINE__, wc.opcode);
				goto error;
		}
	}
	if (ret){
		printk(KERN_ERR PFX "poll error %d\n", ret);
		goto error;
	}
	return;
error:
	cb->state = ERROR;
}

static void IS_setup_wr(struct kernel_cb *cb)
{
	cb->recv_sgl.addr = cb->recv_dma_addr;
	cb->recv_sgl.length = sizeof cb->recv_buf;
	if (cb->local_dma_lkey)
		cb->recv_sgl.lkey = cb->qp->device->local_dma_lkey;
	else if (cb->mem == DMA)
		cb->recv_sgl.lkey = cb->dma_mr->lkey;
	cb->rq_wr.sg_list = &cb->recv_sgl;
	cb->rq_wr.num_sge = 1;

	cb->send_sgl.addr = cb->send_dma_addr;
	cb->send_sgl.length = sizeof cb->send_buf;
	if (cb->local_dma_lkey)
		cb->send_sgl.lkey = cb->qp->device->local_dma_lkey;
	else if (cb->mem == DMA)
		cb->send_sgl.lkey = cb->dma_mr->lkey;
	cb->sq_wr.opcode = IB_WR_SEND;
	cb->sq_wr.send_flags = IB_SEND_SIGNALED;
	cb->sq_wr.sg_list = &cb->send_sgl;
	cb->sq_wr.num_sge = 1;

}

static int IS_setup_buffers(struct kernel_cb *cb)
{
	int ret;

	pr_info(PFX "IS_setup_buffers called on cb %p\n", cb);

	pr_info(PFX "size of IS_rdma_info %lu\n", sizeof(cb->recv_buf));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)	
	cb->recv_dma_addr = dma_map_single(&cb->pd->device->dev, 
				   &cb->recv_buf, sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
#else
	cb->recv_dma_addr = dma_map_single(cb->pd->device->dma_device, 
				   &cb->recv_buf, sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
#endif
	pci_unmap_addr_set(cb, recv_mapping, cb->recv_dma_addr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	cb->send_dma_addr = dma_map_single(&cb->pd->device->dev, 
				   &cb->send_buf, sizeof(cb->send_buf), DMA_BIDIRECTIONAL);	
#else
	cb->send_dma_addr = dma_map_single(cb->pd->device->dma_device, 
					   &cb->send_buf, sizeof(cb->send_buf), DMA_BIDIRECTIONAL);
#endif
	pci_unmap_addr_set(cb, send_mapping, cb->send_dma_addr);
	pr_info(PFX "cb->mem=%d \n", cb->mem);

	if (cb->mem == DMA) {
		pr_info(PFX "IS_setup_buffers, in cb->mem==DMA \n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
		cb->dma_mr = cb->pd->device->get_dma_mr(cb->pd, IB_ACCESS_LOCAL_WRITE|
							        IB_ACCESS_REMOTE_READ|
							        IB_ACCESS_REMOTE_WRITE);
#else
		cb->dma_mr = ib_get_dma_mr(cb->pd, IB_ACCESS_LOCAL_WRITE|
					   IB_ACCESS_REMOTE_READ|
				           IB_ACCESS_REMOTE_WRITE);
#endif
		if (IS_ERR(cb->dma_mr)) {
			pr_info(PFX "reg_dmamr failed\n");
			ret = PTR_ERR(cb->dma_mr);
			goto bail;
		}
	} 
	
	IS_setup_wr(cb);
	pr_info(PFX "allocated & registered buffers...\n");
	return 0;
bail:

	if (cb->rdma_mr && !IS_ERR(cb->rdma_mr))
		ib_dereg_mr(cb->rdma_mr);
	if (cb->dma_mr && !IS_ERR(cb->dma_mr))
		ib_dereg_mr(cb->dma_mr);
	if (cb->recv_mr && !IS_ERR(cb->recv_mr))
		ib_dereg_mr(cb->recv_mr);
	if (cb->send_mr && !IS_ERR(cb->send_mr))
		ib_dereg_mr(cb->send_mr);
	
	return ret;
}

static void IS_free_buffers(struct kernel_cb *cb)
{
	pr_info("IS_free_buffers called on cb %p\n", cb);
	
	if (cb->dma_mr)
		ib_dereg_mr(cb->dma_mr);
	if (cb->send_mr)
		ib_dereg_mr(cb->send_mr);
	if (cb->recv_mr)
		ib_dereg_mr(cb->recv_mr);
	if (cb->rdma_mr)
		ib_dereg_mr(cb->rdma_mr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)	
	dma_unmap_single(&cb->pd->device->dev,
			 pci_unmap_addr(cb, recv_mapping),
			 sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
	dma_unmap_single(&cb->pd->device->dev,
			 pci_unmap_addr(cb, send_mapping),
			 sizeof(cb->send_buf), DMA_BIDIRECTIONAL);
#else
	dma_unmap_single(cb->pd->device->dma_device,
			 pci_unmap_addr(cb, recv_mapping),
			 sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
	dma_unmap_single(cb->pd->device->dma_device,
			 pci_unmap_addr(cb, send_mapping),
			 sizeof(cb->send_buf), DMA_BIDIRECTIONAL);
#endif

}

static int IS_create_qp(struct kernel_cb *cb)
{
	struct ib_qp_init_attr init_attr;
	int ret;

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_send_wr = cb->txdepth; /*FIXME: You may need to tune the maximum work request */
	init_attr.cap.max_recv_wr = cb->txdepth;  
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	init_attr.qp_type = IB_QPT_RC;
	init_attr.send_cq = cb->cq;
	init_attr.recv_cq = cb->cq;

	ret = rdma_create_qp(cb->cm_id, cb->pd, &init_attr);
	if (!ret)
		cb->qp = cb->cm_id->qp;
	return ret;
}

static void IS_free_qp(struct kernel_cb *cb)
{
	ib_destroy_qp(cb->qp);
	ib_destroy_cq(cb->cq);
	ib_dealloc_pd(cb->pd);
}

/*  in ibv_enables, the first step build_connection() from build_context()
		before create_qp
 */
static int IS_setup_qp(struct kernel_cb *cb, struct rdma_cm_id *cm_id)
{
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	struct ib_cq_init_attr init_attr;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	cb->pd = ib_alloc_pd(cm_id->device, IB_ACCESS_LOCAL_WRITE|
                                            IB_ACCESS_REMOTE_READ|
                                            IB_ACCESS_REMOTE_WRITE );
#else
	cb->pd = ib_alloc_pd(cm_id->device);
#endif
	if (IS_ERR(cb->pd)) {
		printk(KERN_ERR PFX "ib_alloc_pd failed\n");
		return PTR_ERR(cb->pd);
	}
	pr_info("created pd %p\n", cb->pd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cqe = cb->txdepth * 2;
	init_attr.comp_vector = 0;
	
	cb->cq = ib_create_cq(cm_id->device, rdma_cq_event_handler, NULL, cb, &init_attr);
#else
	cb->cq = ib_create_cq(cm_id->device, rdma_cq_event_handler, NULL, cb, cb->txdepth * 2, 0);
#endif

	if (IS_ERR(cb->cq)) {
		printk(KERN_ERR PFX "ib_create_cq failed\n");
		ret = PTR_ERR(cb->cq);
		goto err1;
	}
	pr_info("created cq %p\n", cb->cq);

	ret = ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		printk(KERN_ERR PFX "ib_create_cq failed\n");
		goto err2;
	}

	ret = IS_create_qp(cb);
	if (ret) {
		printk(KERN_ERR PFX "IS_create_qp failed: %d\n", ret);
		goto err2;
	}
	pr_info("created qp %p\n", cb->qp);
	return 0;
err2:
	ib_destroy_cq(cb->cq);
err1:
	ib_dealloc_pd(cb->pd);
	return ret;
}

static void fill_sockaddr(struct sockaddr_storage *sin, struct kernel_cb *cb)
{
	memset(sin, 0, sizeof(*sin));

	if (cb->addr_type == AF_INET) {
		struct sockaddr_in *sin4 = (struct sockaddr_in *)sin;
		sin4->sin_family = AF_INET;
		memcpy((void *)&sin4->sin_addr.s_addr, cb->addr, 4);
		sin4->sin_port = cb->port;
	} else if (cb->addr_type == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sin;
		sin6->sin6_family = AF_INET6;
		memcpy((void *)&sin6->sin6_addr, cb->addr, 16);
		sin6->sin6_port = cb->port;
	}
}

static int IS_connect_client(struct kernel_cb *cb)
{
	struct rdma_conn_param conn_param;
	int ret;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 10;

	ret = rdma_connect(cb->cm_id, &conn_param);
	if (ret) {
		printk(KERN_ERR PFX "rdma_connect error %d\n", ret);
		return ret;
	}

	wait_event_interruptible(cb->sem, cb->state >= CONNECTED);
	if (cb->state == ERROR) {
		printk(KERN_ERR PFX "wait for CONNECTED state %d\n", cb->state);
		return -1;
	}

	pr_info("rdma_connect successful\n");
	return 0;
}

static int IS_bind_client(struct kernel_cb *cb)
{
	struct sockaddr_storage sin;
	int ret;

	fill_sockaddr(&sin, cb);

	ret = rdma_resolve_addr(cb->cm_id, NULL, (struct sockaddr *)&sin, 2000);
	if (ret) {
		printk(KERN_ERR PFX "rdma_resolve_addr error %d\n", ret);
		return ret;
	}

	wait_event_interruptible(cb->sem, cb->state >= ROUTE_RESOLVED);
	if (cb->state != ROUTE_RESOLVED) {
		printk(KERN_ERR PFX 
		       "addr/route resolution did not resolve: state %d\n",
		       cb->state);
		return -EINTR;
	}
	pr_info("rdma_resolve_addr - rdma_resolve_route successful\n");
	return 0;
}

const char *IS_device_state_str(struct IS_file *dev)
{
	char *state;

	spin_lock(&dev->state_lock);
	switch (dev->state) {
	case DEVICE_INITIALIZING:
		state = "Initial state";
		break;
	case DEVICE_OPENNING:
		state = "openning";
		break;
	case DEVICE_RUNNING:
		state = "running";
		break;
	case DEVICE_OFFLINE:
		state = "offline";
		break;
	default:
		state = "unknown device state";
	}
	spin_unlock(&dev->state_lock);

	return state;
}

static int rdma_trigger(void *data)
{
	struct IS_session *IS_sess = data;
	unsigned long cur_write_ops;
	unsigned long cur_read_ops;
	unsigned long cur_ops;
	unsigned long filtered_ops;
	unsigned long trigger_threshold = IS_sess->trigger_threshold;
	int w_weight = IS_sess->w_weight;
	int r_weight = 100 - w_weight;
	int cur_weight = IS_sess->cur_weight;
	int last_weight = 100 - cur_weight;
	int i = 0;
	int map_res = -1;
	int map_count = 0;

	pr_info("%s\n", __func__);

	for (i=0; i<STACKBD_SIZE_G; i++){
		IS_sess->write_ops[i] = 0;
		IS_sess->read_ops[i] = 0;
	}

	while (1) {
		for (i=0; i<STACKBD_SIZE_G; i++){
			spin_lock_irq(&IS_sess->write_ops_lock[i]);
			cur_write_ops = IS_sess->write_ops[i];
			IS_sess->write_ops[i] = 0;
			spin_unlock_irq(&IS_sess->write_ops_lock[i]);
			spin_lock_irq(&IS_sess->read_ops_lock[i]);
			cur_read_ops = IS_sess->read_ops[i];
			IS_sess->read_ops[i] = 0;
			spin_unlock_irq(&IS_sess->read_ops_lock[i]);
			cur_ops = (unsigned long)(w_weight * cur_write_ops + r_weight * cur_read_ops);
			filtered_ops = (unsigned long)(cur_weight * cur_ops + last_weight * IS_sess->last_ops[i]);
			IS_sess->last_ops[i] = filtered_ops;
			if (filtered_ops > trigger_threshold) {
				if (atomic_read(&IS_sess->trigger_enable) == TRIGGER_ON){
					if (atomic_read(IS_sess->cb_index_map + i) == NO_CB_MAPPED ){
						do {
							map_res = IS_single_chunk_map(IS_sess, i);
							map_count += 1;
						} while (map_res == -1 && map_count < 1);
						map_count = 0;
					}
				}
			}
		}
		msleep(RDMA_TRIGGER_PERIOD);
	}	

	return 0;
}

int IS_create_device(struct IS_session *IS_session,
					   const char *xdev_name, struct IS_file *IS_file)
{
	int retval;
	// char name[20];
	sscanf(xdev_name, "%s", IS_file->file_name);
	IS_file->index = IS_indexes++;
	IS_file->nr_queues = submit_queues;
	IS_file->queue_depth = IS_QUEUE_DEPTH;
	IS_file->IS_conns = IS_session->IS_conns;
	pr_info("In IS_create_device(), dev_name:%s\n", xdev_name);
	retval = IS_setup_queues(IS_file); // prepare enough queue items for each working threads
	if (retval) {
		pr_err("%s: IS_setup_queues failed\n", __func__);
		goto err;
	}
	IS_file->stbuf.st_size = IS_session->capacity;
	pr_info(PFX "st_size = %llu\n", IS_file->stbuf.st_size);
	IS_session->xdev = IS_file;
	retval = IS_register_block_device(IS_file);
	if (retval) {
		pr_err("failed to register IS device %s ret=%d\n",
		       IS_file->file_name, retval);
		goto err_queues;
	}

	IS_set_device_state(IS_file, DEVICE_RUNNING);
	msleep(10000);
	wake_up_process(IS_session->rdma_trigger_thread);	
	return 0;

err_queues:
	IS_destroy_queues(IS_file);
err:
	return retval;
}

void IS_destroy_device(struct IS_session *IS_session,
                         struct IS_file *IS_file)
{
	pr_info("%s\n", __func__);

	IS_set_device_state(IS_file, DEVICE_OFFLINE);
	if (IS_file->disk){
		IS_unregister_block_device(IS_file);  
		IS_destroy_queues(IS_file);  
	}

	spin_lock(&IS_session->devs_lock);
	list_del(&IS_file->list);
	spin_unlock(&IS_session->devs_lock);
}

static void IS_destroy_session_devices(struct IS_session *IS_session)
{
	struct IS_file *xdev, *tmp;
	pr_info("%s\n", __func__);
	list_for_each_entry_safe(xdev, tmp, &IS_session->devs_list, list) {
		IS_destroy_device(IS_session, xdev);
	}
}

static void IS_destroy_conn(struct IS_connection *IS_conn)
{
	IS_conn->IS_sess = NULL;
	IS_conn->conn_th = NULL;
	pr_info("%s\n", __func__);

	kfree(IS_conn);
}

static int IS_ctx_init(struct IS_connection *IS_conn, struct kernel_cb *cb, int cb_index)
{
	struct rdma_ctx *ctx;	
	int i=0;
	int ret = 0;
	struct ctx_pool_list *tmp_pool = IS_conn->ctx_pools[cb_index];
	tmp_pool->free_ctxs = (struct free_ctx_pool *)kzalloc(sizeof(struct free_ctx_pool), GFP_KERNEL);
	tmp_pool->free_ctxs->len = IS_QUEUE_DEPTH;
	spin_lock_init(&tmp_pool->free_ctxs->ctx_lock);
	tmp_pool->free_ctxs->head = 0;
	tmp_pool->free_ctxs->tail = IS_QUEUE_DEPTH - 1;
	tmp_pool->free_ctxs->ctx_list = (struct rdma_ctx **)kzalloc(sizeof(struct rdma_ctx *) * IS_QUEUE_DEPTH, GFP_KERNEL);
	tmp_pool->ctx_pool = (struct rdma_ctx *)kzalloc(sizeof(struct rdma_ctx) * IS_QUEUE_DEPTH, GFP_KERNEL);


	for (i=0; i < IS_QUEUE_DEPTH; i++){
		ctx = tmp_pool->ctx_pool + i;
		tmp_pool->free_ctxs->ctx_list[i] = ctx;

		atomic_set(&ctx->in_flight, CTX_IDLE);
		ctx->chunk_index = -1;
		ctx->req = NULL;
		ctx->IS_conn = IS_conn;
		ctx->free_ctxs = tmp_pool->free_ctxs;
		ctx->rdma_buf = kzalloc(cb->size, GFP_KERNEL);
		if (!ctx->rdma_buf) {
			pr_info(PFX "rdma_buf malloc failed\n");
			ret = -ENOMEM;
			goto bail;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
		ctx->rdma_dma_addr = dma_map_single(&cb->pd->device->dev,
                                       ctx->rdma_buf, cb->size,
                                       DMA_BIDIRECTIONAL);
#else
		ctx->rdma_dma_addr = dma_map_single(cb->pd->device->dma_device, 
				       ctx->rdma_buf, cb->size, 
				       DMA_BIDIRECTIONAL);
#endif
		pci_unmap_addr_set(ctx, rdma_mapping, ctx->rdma_dma_addr);	

		// rdma_buf, peer nodes RDMA write destination
		ctx->rdma_sgl.addr = ctx->rdma_dma_addr;
		ctx->rdma_sgl.lkey = cb->qp->device->local_dma_lkey;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		ctx->rdma_sq_wr.wr.send_flags = IB_SEND_SIGNALED;
		ctx->rdma_sq_wr.wr.sg_list = &ctx->rdma_sgl;
		ctx->rdma_sq_wr.wr.num_sge = 1;
		ctx->rdma_sq_wr.wr.wr_id = uint64_from_ptr(ctx);
#else
		ctx->rdma_sq_wr.send_flags = IB_SEND_SIGNALED;
		ctx->rdma_sq_wr.sg_list = &ctx->rdma_sgl;
		ctx->rdma_sq_wr.num_sge = 1;
		ctx->rdma_sq_wr.wr_id = uint64_from_ptr(ctx);
#endif
	}
	return 0;

bail:
	kfree(ctx->rdma_buf);
	return ret;	
}

static int IS_create_conn(struct IS_session *IS_session, int cpu,
			    struct IS_connection **conn)
{
	struct IS_connection *IS_conn;
	int ret = 0;
	int i;	
	pr_info("%s with cpu: %d\n", __func__, cpu);

	IS_conn = kzalloc(sizeof(*IS_conn), GFP_KERNEL);
	if (!IS_conn) {
		pr_err("failed to allocate IS_conn");
		return -ENOMEM;
	}
	IS_conn->IS_sess = IS_session;
	IS_conn->cpu_id = cpu;

	IS_conn->ctx_pools = (struct ctx_pool_list **)kzalloc(sizeof(struct ctx_pool_list *) * NUM_CB, GFP_KERNEL);
	for (i=0; i<NUM_CB; i++){
		IS_conn->ctx_pools[i] = (struct ctx_pool_list *)kzalloc(sizeof(struct ctx_pool_list), GFP_KERNEL);
	}

	*conn = IS_conn;

	return ret;
}
static int rdma_connect_down(struct kernel_cb *cb)
{
	struct ib_recv_wr *bad_wr;
	int ret;

	ret = ib_post_recv(cb->qp, &cb->rq_wr, &bad_wr); 
	if (ret) {
		printk(KERN_ERR PFX "ib_post_recv failed: %d\n", ret);
		goto err;
	}

	ret = IS_connect_client(cb);  
	if (ret) {
		printk(KERN_ERR PFX "connect error %d\n", ret);
		goto err;
	}

	return 0;

err:
	IS_free_buffers(cb);
	return ret;
}

static int rdma_connect_upper(struct kernel_cb *cb)
{
	int ret;
	ret = IS_bind_client(cb);
	if (ret)
		return ret;
	ret = IS_setup_qp(cb, cb->cm_id);
	if (ret) {
		printk(KERN_ERR PFX "setup_qp failed: %d\n", ret);
		return ret;
	}
	ret = IS_setup_buffers(cb);
	if (ret) {
		printk(KERN_ERR PFX "IS_setup_buffers failed: %d\n", ret);
		goto err1;
	}
	return 0;
err1:
	IS_free_qp(cb);	
	return ret;
}

static void portal_parser(struct IS_session *IS_session)
{
	//portal format rdma://2,192.168.0.12:8000,192.168.0.11:9400
	char *ptr = IS_session->portal + 7;	//rdma://[]
	char *single_portal = NULL;
	int p_count=0, i=0, j=0;
	int port = 0;

	sscanf(strsep(&ptr, ","), "%d", &p_count);
	NUM_CB = p_count;
	IS_session->cb_num = NUM_CB;
	IS_session->portal_list = kzalloc(sizeof(struct IS_portal) * IS_session->cb_num, GFP_KERNEL);	

	for (; i < p_count; i++){
		single_portal = strsep(&ptr, ",");

		j = 0;
		while (*(single_portal + j) != ':'){
			j++;
		}
		memcpy(IS_session->portal_list[i].addr, single_portal, j);
		IS_session->portal_list[i].addr[j] = '\0';
		port = 0;
		sscanf(single_portal+j+1, "%d", &port);
		IS_session->portal_list[i].port = (uint16_t)port; 
		pr_err("portal: %s, %d\n", IS_session->portal_list[i].addr, IS_session->portal_list[i].port);
	}	
}

static int kernel_cb_init(struct kernel_cb *cb, struct IS_session *IS_session)
{
	int ret = 0;
	int i;
	cb->IS_sess = IS_session;
	cb->addr_type = AF_INET;
	cb->mem = DMA;
	cb->txdepth = IS_QUEUE_DEPTH * submit_queues + 1;
	cb->size = IS_PAGE_SIZE * MAX_SGL_LEN; 
	cb->state = IDLE;

	cb->remote_chunk.chunk_size_g = 0;
	cb->remote_chunk.chunk_list = (struct remote_chunk_g **)kzalloc(sizeof(struct remote_chunk_g *) * MAX_MR_SIZE_GB, GFP_KERNEL);
	cb->remote_chunk.remote_mapped = (atomic_t *)kmalloc(sizeof(atomic_t) * MAX_MR_SIZE_GB, GFP_KERNEL);
	cb->remote_chunk.chunk_map = (int *)kzalloc(sizeof(int) * MAX_MR_SIZE_GB, GFP_KERNEL);
	cb->remote_chunk.evict_chunk_map = (char *)kzalloc(sizeof(char) * MAX_MR_SIZE_GB, GFP_KERNEL);
	for (i=0; i < MAX_MR_SIZE_GB; i++){
		atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_UNMAPPED);
		cb->remote_chunk.chunk_map[i] = -1;
		cb->remote_chunk.chunk_list[i] = (struct remote_chunk_g *)kzalloc(sizeof(struct remote_chunk_g), GFP_KERNEL); 
		cb->remote_chunk.evict_chunk_map[i] = 0x00;
	}

	init_waitqueue_head(&cb->sem);

	init_waitqueue_head(&cb->remote_chunk.sem);
	cb->remote_chunk.c_state = C_IDLE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	cb->cm_id = rdma_create_id(&init_net, IS_cma_event_handler, cb, RDMA_PS_TCP, IB_QPT_RC);
#else
	cb->cm_id = rdma_create_id(IS_cma_event_handler, cb, RDMA_PS_TCP, IB_QPT_RC);
#endif
	if (IS_ERR(cb->cm_id)) {
		ret = PTR_ERR(cb->cm_id);
		printk(KERN_ERR PFX "rdma_create_id error %d\n", ret);
		goto out;
	} 
	pr_info("%s, created cm_id %p\n", __func__, cb->cm_id);
	return 0;
out:
	kfree(cb);
	return ret;
}

void IS_ctx_dma_setup(struct kernel_cb *cb, struct IS_session *IS_session, int cb_index)
{
	struct IS_connection *IS_conn;
	int i;

	for (i=0; i<submit_queues; i++){
		IS_conn = IS_session->IS_conns[i];
		IS_conn->cbs = IS_session->cb_list;
		IS_ctx_init(IS_conn, cb, cb_index);
	}
	pr_info("%s, setup_ctx_dma\n", __func__);
}

int IS_single_chunk_map(struct IS_session *IS_session, int select_chunk)
{
	int i, j, k;
	char name[2];
	struct kernel_cb *tmp_cb;
	int selection[SERVER_SELECT_NUM];
	int free_mem[SERVER_SELECT_NUM];
	int free_mem_sorted[SERVER_SELECT_NUM]; 
	int cb_index;
	int need_chunk;
	int avail_cb;
	unsigned int random_cb_selection[NUM_CB];
	unsigned int random_num;

	for (j = 0; j < SERVER_SELECT_NUM; j++){
		selection[j] = NUM_CB; //no server 
		free_mem[j] = -1;
		free_mem_sorted[j] = NUM_CB;
	}
	need_chunk = select_chunk;
	j = 0;

	avail_cb = NUM_CB;
	for (i=0; i<NUM_CB;i++){
		random_cb_selection[i] = -1;
		if (IS_session->cb_state_list[i] >= CB_EVICTING) {
			avail_cb -= 1;
		}
	}

	if (avail_cb <= SERVER_SELECT_NUM) { 
		for (i=0; i<IS_session->cb_num; i++){
			if (IS_session->cb_state_list[i] < CB_EVICTING){
				selection[j] = i;	
				j += 1;
			}
		}
	}else { 
		for (j=0; j<SERVER_SELECT_NUM;j++){
			get_random_bytes(&random_num, sizeof(unsigned int));
			random_num %= NUM_CB;
			while (IS_session->cb_state_list[random_num] >= CB_EVICTING || random_cb_selection[random_num] == 1) {
				random_num += 1;	
				random_num %= NUM_CB;
			}
			selection[j] = random_num;
			random_cb_selection[random_num] = 1;
		}
	}

	k = j;  
	if (k == 0) {
		return -1;	
	}

	for (i=0; i < k; i++){
		cb_index = selection[i];
		if (IS_session->cb_state_list[cb_index] == CB_FAIL){
			continue;	
		}
		tmp_cb = IS_session->cb_list[cb_index];
		if (IS_session->cb_state_list[cb_index] > CB_IDLE) {
			IS_send_query(tmp_cb);				
			wait_event_interruptible(tmp_cb->sem, tmp_cb->state == FREE_MEM_RECV);
			tmp_cb->state = AFTER_FREE_MEM;
			free_mem[i] = tmp_cb->remote_chunk.target_size_g;
			free_mem_sorted[i] = cb_index;
		}else { //CB_IDLE
			kernel_cb_init(tmp_cb, IS_session);
			rdma_connect_upper(tmp_cb);	
			rdma_connect_down(tmp_cb);	
			wait_event_interruptible(tmp_cb->sem, tmp_cb->state == FREE_MEM_RECV);
			tmp_cb->state = AFTER_FREE_MEM;
			IS_session->cb_state_list[cb_index] = CB_CONNECTED; //add CB_CONNECTED		
			free_mem[i] = tmp_cb->remote_chunk.target_size_g;
			free_mem_sorted[i] = cb_index;
		}
	}
	for (j=1; j<k; j++) {
		if (free_mem[0] < free_mem[j]) {
			free_mem[0] += free_mem[j];	
			free_mem[j] = free_mem[0] - free_mem[j];
			free_mem[0] = free_mem[0] - free_mem[j];
			free_mem_sorted[0] += free_mem_sorted[j];	
			free_mem_sorted[j] = free_mem_sorted[0] - free_mem_sorted[j];
			free_mem_sorted[0] = free_mem_sorted[0] - free_mem_sorted[j];
		}
	}

	if (free_mem[0] == 0){
		return -1;
	}
	cb_index = free_mem_sorted[0];
	tmp_cb = IS_session->cb_list[cb_index];
	if (IS_session->cb_state_list[cb_index] == CB_CONNECTED){ 
		IS_session->mapped_cb_num += 1;
		IS_ctx_dma_setup(tmp_cb, IS_session, cb_index); 
		memset(name, '\0', 2);
		name[0] = (char)((cb_index/26) + 97);
		tmp_cb->remote_chunk.evict_handle_thread = kthread_create(evict_handler, tmp_cb, name);
		wake_up_process(tmp_cb->remote_chunk.evict_handle_thread);	
	}
	IS_send_bind_single(tmp_cb, need_chunk);
	wait_event_interruptible(tmp_cb->sem, tmp_cb->state == WAIT_OPS);
	atomic_set(&IS_session->rdma_on, DEV_RDMA_ON); 
	return need_chunk;
}

int IS_session_create(const char *portal, struct IS_session *IS_session)
{
	int i, j, ret;
	char name[20];
	printk(KERN_ALERT "In IS_session_create() with portal: %s\n", portal);
	
	memcpy(IS_session->portal, portal, strlen(portal));
	pr_err("%s\n", IS_session->portal);
	portal_parser(IS_session);

	IS_session->capacity_g = STACKBD_SIZE_G; 
	IS_session->capacity = (unsigned long long)STACKBD_SIZE_G * ONE_GB;
	IS_session->mapped_cb_num = 0;
	IS_session->mapped_capacity = 0;
	IS_session->cb_list = (struct kernel_cb **)kzalloc(sizeof(struct kernel_cb *) * IS_session->cb_num, GFP_KERNEL);	
	IS_session->cb_state_list = (enum cb_state *)kzalloc(sizeof(enum cb_state) * IS_session->cb_num, GFP_KERNEL);
	for (i=0; i<IS_session->cb_num; i++) {
		IS_session->cb_state_list[i] = CB_IDLE;	
		IS_session->cb_list[i] = kzalloc(sizeof(struct kernel_cb), GFP_KERNEL);
		IS_session->cb_list[i]->port = htons(IS_session->portal_list[i].port);
		in4_pton(IS_session->portal_list[i].addr, -1, IS_session->cb_list[i]->addr, -1, NULL);
		IS_session->cb_list[i]->cb_index = i;
	}

	IS_session->cb_index_map = kzalloc(sizeof(atomic_t) * IS_session->capacity_g, GFP_KERNEL);
	IS_session->chunk_map_cb_chunk = (int*)kzalloc(sizeof(int) * IS_session->capacity_g, GFP_KERNEL);
	IS_session->unmapped_chunk_list = (int*)kzalloc(sizeof(int) * IS_session->capacity_g, GFP_KERNEL);
	IS_session->free_chunk_index = IS_session->capacity_g - 1;
	for (i = 0; i < IS_session->capacity_g; i++){
		atomic_set(IS_session->cb_index_map + i, NO_CB_MAPPED);
		IS_session->unmapped_chunk_list[i] = IS_session->capacity_g-1-i;
		IS_session->chunk_map_cb_chunk[i] = -1;
	}

	for (i=0; i < STACKBD_SIZE_G; i++){
		spin_lock_init(&IS_session->write_ops_lock[i]);
		spin_lock_init(&IS_session->read_ops_lock[i]);
		IS_session->write_ops[i] = 0;
		IS_session->read_ops[i] = 0;
		IS_session->last_ops[i] = 0;
	}
	IS_session->trigger_threshold = RDMA_TRIGGER_THRESHOLD;
	IS_session->w_weight = RDMA_W_WEIGHT;
	IS_session->cur_weight = RDMA_CUR_WEIGHT;
	atomic_set(&IS_session->trigger_enable, TRIGGER_ON);

	IS_session->read_request_count = (unsigned long*)kzalloc(sizeof(unsigned long) * submit_queues, GFP_KERNEL);
	IS_session->write_request_count = (unsigned long*)kzalloc(sizeof(unsigned long) * submit_queues, GFP_KERNEL);

	//IS-connection
	IS_session->IS_conns = (struct IS_connection **)kzalloc(submit_queues * sizeof(*IS_session->IS_conns), GFP_KERNEL);
	if (!IS_session->IS_conns) {
		pr_err("failed to allocate IS connections array\n");
		ret = -ENOMEM;
		goto err_destroy_portal;
	}
	for (i = 0; i < submit_queues; i++) {
		IS_session->read_request_count[i] = 0;	
		IS_session->write_request_count[i] = 0;	
		ret = IS_create_conn(IS_session, i, &IS_session->IS_conns[i]);
		if (ret)
			goto err_destroy_conns;
	}
	atomic_set(&IS_session->rdma_on, DEV_RDMA_OFF);

	strcpy(name, "rdma_trigger_thread");
	IS_session->rdma_trigger_thread = kthread_create(rdma_trigger, IS_session, name);

	return 0;

err_destroy_conns:
	for (j = 0; j < i; j++) {
		IS_destroy_conn(IS_session->IS_conns[j]);
		IS_session->IS_conns[j] = NULL;
	}
	kfree(IS_session->IS_conns);
err_destroy_portal:

	return ret;
}

void IS_session_destroy(struct IS_session *IS_session)
{
	mutex_lock(&g_lock);
	list_del(&IS_session->list);
	mutex_unlock(&g_lock);

	pr_info("%s\n", __func__);
	IS_destroy_session_devices(IS_session);
}

static int __init IS_init_module(void)
{
	if (IS_create_configfs_files())
		return 1;

	pr_debug("nr_cpu_ids=%d, num_online_cpus=%d\n", nr_cpu_ids, num_online_cpus());

	submit_queues = num_online_cpus();

	IS_major = register_blkdev(0, "infiniswap");
	if (IS_major < 0)
		return IS_major;

	mutex_init(&g_lock);
	INIT_LIST_HEAD(&g_IS_sessions);

	return 0;
}

// module function
static void __exit IS_cleanup_module(void)
{
	struct IS_session *IS_session, *tmp;

	unregister_blkdev(IS_major, "infiniswap");

	list_for_each_entry_safe(IS_session, tmp, &g_IS_sessions, list) {
		IS_session_destroy(IS_session);
	}

	IS_destroy_configfs_files();

}

module_init(IS_init_module);
module_exit(IS_cleanup_module);
