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
#include <linux/bio.h>

#ifdef EC
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
#include <asm/fpu/api.h>
#else
#include <asm/i387.h>
#endif

#include "erasure_code/erasure_code.h"

unsigned char *encode_matrix, *decode_matrix, *invert_matrix, *encode_tbls, *decode_tbls;
int nerrs, nsrcerrs=0;
unsigned char src_in_err[NDISKS], src_err_list[NDISKS];
unsigned int decode_index[NDATAS];

#define NO_INVERT_MATRIX -2
// Generate decode matrix from encode matrix
static int gf_gen_decode_matrix(unsigned char *encode_matrix,
				unsigned char *decode_matrix,
				unsigned char *invert_matrix,
				unsigned int *decode_index,
				unsigned char *src_err_list,
				unsigned char *src_in_err,
				int nerrs, int nsrcerrs, int k, int m)
{
	int i, j, p;
	int r;
	unsigned char backup[NDISKS*NDATAS], b[NDISKS*NDATAS], s;
	int incr = 0;

	// Construct matrix b by removing error rows
	for (i = 0, r = 0; i < k; i++, r++) {
		while (src_in_err[r])
			r++;
		for (j = 0; j < k; j++) {
			b[k * i + j] = encode_matrix[k * r + j];
			backup[k * i + j] = encode_matrix[k * r + j];
		}
		decode_index[i] = r;
	}
	incr = 0;
	while (gf_invert_matrix(b, invert_matrix, k) < 0) {
		if (nerrs == (m - k)) {
			printk("%s:%d BAD MATRIX\n", __FILE__, __LINE__ );
			return NO_INVERT_MATRIX;
		}
		incr++;
		memcpy(b, backup, NDISKS * NDATAS);
		for (i = nsrcerrs; i < nerrs - nsrcerrs; i++) {
			if (src_err_list[i] == (decode_index[k - 1] + incr)) {
				// skip the erased parity line
				incr++;
				continue;
			}
		}
		if (decode_index[k - 1] + incr >= m) {
			printk("%s:%d BAD MATRIX\n", __FILE__, __LINE__ );
			return NO_INVERT_MATRIX;
		}
		decode_index[k - 1] += incr; 
		for (j = 0; j < k; j++)
			b[k * (k - 1) + j] = encode_matrix[k * decode_index[k - 1] + j];

	};

	for (i = 0; i < nsrcerrs; i++) {
		for (j = 0; j < k; j++) {
			decode_matrix[k * i + j] = invert_matrix[k * src_err_list[i] + j];
		}
	}
	/* src_err_list from encode_matrix * invert of b for parity decoding */
	for (p = nsrcerrs; p < nerrs; p++) {
		for (i = 0; i < k; i++) {
			s = 0;
			for (j = 0; j < k; j++)
				s ^= gf_mul(invert_matrix[j * k + i],
					    encode_matrix[k * src_err_list[p] + j]);

			decode_matrix[k * p + i] = s;
		}
	}
	return 0;
}

#endif

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



void show_rdma_info(struct IS_rdma_info * info){

	printk("type: %d\nindex: %d\n",
		info->type, info->size_gb);

}


static void rdma_cq_event_handler(struct ib_cq * cq, struct kernel_cb *cb);


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


int IS_rdma_read(struct IS_connection *IS_conn, struct kernel_cb **cb, int *cb_index, int *chunk_index, struct remote_chunk_g **chunk, unsigned long offset, unsigned long len, struct request *req, struct IS_queue *q)
{
	int ret;
	struct ib_send_wr *bad_wr;
	struct rdma_ctx *ctx[NDISKS];
	int i,j;

//        printk("read [%x] len [%x]", offset, len );
	
	/*ctx synchronization*/	
	atomic_t* count = kzalloc(sizeof(atomic_t), GFP_KERNEL);
	atomic_set(count, NDATAS);

	
	// get ctx_buf based on request address
	for (i=0; i<NDISKS; i++){
 	int conn_id = (uint64_t)( bio_data(req->bio)   ) & QUEUE_NUM_MASK;

	IS_conn = IS_conn->IS_sess->IS_conns[conn_id];

	if (cb_index[i] == NO_CB_MAPPED){
	ctx[i] = IS_get_ctx(IS_conn->ctx_pools[i]);
	continue;
	}

	ctx[i] = IS_get_ctx(IS_conn->ctx_pools[  cb_index[i] ]);
	BUG_ON(!ctx[i]); //ctx NULL

	ctx[i]->req = req;
	ctx[i]->cb = cb[i];
	//ctx[i]->chunk_index = chunk_index[i]; //chunk_index in cb
	atomic_set(&ctx[i]->in_flight, CTX_R_IN_FLIGHT);  
	

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	ctx[i]->rdma_sq_wr.wr.sg_list->length = (len/NDATAS);
	ctx[i]->rdma_sq_wr.rkey = chunk[i]->remote_rkey;
	ctx[i]->rdma_sq_wr.remote_addr = chunk[i]->remote_addr + (offset/NDATAS);
	ctx[i]->rdma_sq_wr.wr.opcode = IB_WR_RDMA_READ;
 #else
	ctx[i]->rdma_sq_wr.sg_list->length = (len/NDATAS);
	ctx[i]->rdma_sq_wr.wr.rdma.rkey = chunk[i]->remote_rkey;
	ctx[i]->rdma_sq_wr.wr.rdma.remote_addr = chunk[i]->remote_addr + (offset/NDATAS);
	ctx[i]->rdma_sq_wr.opcode = IB_WR_RDMA_READ;
 #endif

	ctx[i]->index = i;
	ctx[i]->cnt = count;
	}


	for (i=0; i<NDISKS; i++){
		for (j=0; j<NDISKS; j++){
		ctx[i]->ctxs[j] = ctx[j];
		}
	}

	for (i=0, j=0; i<NDATAS; i++){	
	if(cb_index[i] == NO_CB_MAPPED){
		for (; cb_index[NDATAS+j] == NO_CB_MAPPED; j++); //looking for good parity
		
		if (j < NDISKS-NDATAS){
		ret = ib_post_send(cb[NDATAS+j]->qp, (struct ib_send_wr *) &ctx[NDATAS+j]->rdma_sq_wr, &bad_wr);
		if (ret){
			printk(KERN_ALERT PFX "client post read %d, wr=%p\n", ret, &ctx[i]->rdma_sq_wr);
			return ret;
		}
		j++;
		}
		else {
		printk(KERN_ALERT PFX "client post read %d, wr=%p\n", ret, &ctx[i]->rdma_sq_wr);
		return ret;
		}
	}
	else{
		ret = ib_post_send(cb[i]->qp, (struct ib_send_wr *) &ctx[i]->rdma_sq_wr, &bad_wr); //not returning error
		//printk("client post read cb: %d offset:%lu len: %lu\n", cb_index[i], offset/NDATAS, len/NDATAS );
		if (ret){
			printk(KERN_ALERT PFX "client post read %d, wr=%p\n", ret, &ctx[i]->rdma_sq_wr);
			return ret;
		}
	
	}
	}
	
	for (i=0; i < NDATAS+j; i++) {
	if(cb_index[i] == NO_CB_MAPPED)
		continue;
	//printk("waiting for read cb: %d offset:%lu len: %lu\n", cb_index[i], offset/NDATAS, len/NDATAS );
	rdma_cq_event_handler(cb[i]->cq, cb[i]);
	}



	return 0;

}


int IS_rdma_write(struct IS_connection *IS_conn, struct kernel_cb **cb, int *cb_index, int *chunk_index, struct remote_chunk_g **chunk, unsigned long offset, unsigned long len, struct request *req, struct IS_queue *q)
{
        int ret;
        struct ib_send_wr *bad_wr;
        struct rdma_ctx *ctx[NDISKS];
	int i;
	unsigned char *ptrs[NDISKS];
	struct bio *bio =  req->bio;

//      printk("write [%x] len [%x]", offset, len  );

	atomic_t* count = kzalloc(sizeof(atomic_t), GFP_KERNEL);
	atomic_set(count, NDATAS);

	for (i=0; i<NDISKS; i++){ // we need to get all the ctxs // write needs to be suspended until new chunk found
        /* distribute based on request address for load balancing*/
 	int conn_id = (uint64_t)( bio_data(req->bio)   ) & QUEUE_NUM_MASK;

        IS_conn = IS_conn->IS_sess->IS_conns[conn_id];

	if (cb_index[i] == NO_CB_MAPPED){
	ctx[i] = IS_get_ctx(IS_conn->ctx_pools[i]);
	ptrs[i] = ctx[i]->rdma_buf;
	continue;
	}
	
        ctx[i] = IS_get_ctx(IS_conn->ctx_pools[ cb_index[i] ]); 
	BUG_ON(!ctx[i]);

        ctx[i]->req = req;
        ctx[i]->cb = cb[i];
        ctx[i]->offset = (offset/NDATAS);
        ctx[i]->len = (len/NDATAS);
        ctx[i]->chunk_ptr = chunk[i];
        ctx[i]->chunk_index = chunk_index[i];

	ptrs[i] = ctx[i]->rdma_buf;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
        ctx[i]->rdma_sq_wr.wr.sg_list->length = (len/NDATAS);
        ctx[i]->rdma_sq_wr.rkey = chunk[i]->remote_rkey;
        ctx[i]->rdma_sq_wr.remote_addr = chunk[i]->remote_addr + (offset/NDATAS);
        ctx[i]->rdma_sq_wr.wr.opcode = IB_WR_RDMA_WRITE;
#else
        ctx[i]->rdma_sq_wr.sg_list->length = (len/NDATAS);
        ctx[i]->rdma_sq_wr.wr.rdma.rkey = chunk[i]->remote_rkey;
        ctx[i]->rdma_sq_wr.wr.rdma.remote_addr = chunk[i]->remote_addr + (offset/NDATAS);
        ctx[i]->rdma_sq_wr.opcode = IB_WR_RDMA_WRITE;

#endif
	ctx[i]->index = i;
	ctx[i]->cnt = count;
	}

#ifdef REP
	for (bio_offset=0; bio; bio = bio->bi_next, bio_offset+=IS_PAGE_SIZE){
	for (i=0; i< NDISKS; i++){

        memcpy(ctx[i]->rdma_buf + bio_offset,
	       bio_data(bio),
	       IS_PAGE_SIZE );
	}
	}
#endif



#ifdef EC

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (bio_multiple_segments(bio)){
        /*read can have segments */
        struct bio_vec bvl;
        struct bvec_iter iter;
        u32  bio_offset= 0;

        bio_for_each_segment(bvl, bio, iter){
        BUG_ON(bvl.bv_len!= IS_PAGE_SIZE); //each bvec is page
        for(i=0; i<NDATAS; i++){
        memcpy( ctx[i]->rdma_buf + bio_offset/NDATAS,
		page_address(bvl.bv_page) + i*IS_PAGE_SIZE/NDATAS,
                IS_PAGE_SIZE/NDATAS);
        }
        bio_offset += IS_PAGE_SIZE;
        }
        }
	else
#endif
	{
	u32  bio_offset;	
	for (bio_offset=0; bio; bio = bio->bi_next, bio_offset+=IS_PAGE_SIZE){
//	BUG_ON(bio_multiple_segments(bio)); 
	for (i=0; i<NDATAS; i++){

	memcpy( ctx[i]->rdma_buf + bio_offset/NDATAS,
	        bio_data(bio) + i*IS_PAGE_SIZE/NDATAS , 
	        IS_PAGE_SIZE/NDATAS );

	}
	}
	}

	kernel_fpu_begin();
	ec_encode_data_avx(len/NDATAS, NDATAS, NDISKS-NDATAS, encode_tbls, ptrs, &ptrs[NDATAS]);
	kernel_fpu_end();
#endif


	for (i=0; i<NDATAS; i++){
	if(cb_index[i] == NO_CB_MAPPED)
		continue;		
	//printk("client post write cb: %d offset:%lu len: %lu\n", cb_index[i], offset/NDATAS, len/NDATAS );
        ret = ib_post_send(cb[i]->qp, (struct ib_send_wr *) &ctx[i]->rdma_sq_wr, &bad_wr);
        if (ret) {
                printk(KERN_ALERT PFX "client post write %d, wr=%p\n", ret, &ctx[i]->rdma_sq_wr);
                return ret;
        }
	}



	for (i=NDATAS; i<NDISKS; i++){
	if(cb_index[i] == NO_CB_MAPPED)
		continue;
	//printk("client post write cb: %d offset:%lu len: %lu\n", cb_index[i], offset/NDATAS, len/NDATAS );
        ret = ib_post_send(cb[i]->qp, (struct ib_send_wr *) &ctx[i]->rdma_sq_wr, &bad_wr);
        if (ret) {
                printk(KERN_ALERT PFX "client post write %d, wr=%p\n", ret, &ctx[i]->rdma_sq_wr);
                return ret;
        }
	}

	for (i=0; i<NDISKS; i++){
	if(cb_index[i] == NO_CB_MAPPED){
		IS_insert_ctx( ctx[i] );			
		continue;
	}
	//printk("waiting for write cb: %d offset:%lu len: %lu\n", cb_index[i], offset/NDATAS, len/NDATAS );
        rdma_cq_event_handler(cb[i]->cq, cb[i]);
	}

        return 0;
}

static int IS_send_activity(struct kernel_cb *cb)
{
	int ret = 0;
	struct ib_send_wr *bad_wr;	
	int i;
	int count=0;
	int chunk_sess_index = -1;
	cb->send_buf.type = ACTIVITY;

	for (i=0; i<MAX_MR_SIZE_GB; i++) {
		chunk_sess_index = cb->remote_chunk.chunk_map[i];
		if (chunk_sess_index != -1){ //mapped chunk
			cb->send_buf.buf[i] = 0; //htonll( (IS_sess->last_ops[chunk_sess_index] + 1) ); //how much slabs being used> //FIXME
			count += 1;
		}else { //unmapped chunk
			cb->send_buf.buf[i] = 0;	
		}
	}
	ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR PFX "ACTIVITY MSG send error %d\n", ret);
		return ret;
	}
	rdma_cq_event_handler(cb->cq, cb);

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
	rdma_cq_event_handler(cb->cq, cb);

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
	rdma_cq_event_handler(cb->cq, cb);

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

int IS_transfer_chunk(struct IS_file *xdev, struct kernel_cb **cb, int *cb_index, int *chunk_index, struct remote_chunk_g **chunk, unsigned long offset,
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
	int ret;
	int evict_list[DISKSIZE_G];
	struct request *req;

	pr_debug("%s\n", __func__);

	for (i=0; i<DISKSIZE_G;i++){
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

	/*construct decode matrix*/	
	decode_matrix = kzalloc(NDISKS * NDATAS * sizeof(unsigned char), GFP_KERNEL);
	invert_matrix = kzalloc(NDISKS * NDATAS * sizeof(unsigned char), GFP_KERNEL);
	decode_tbls   = kzalloc((NDISKS-NDATAS) * NDATAS * 32 * sizeof(unsigned char), GFP_KERNEL);

	memset(src_in_err, 0, NDISKS);
	//memset(src_err_list, 0, NDISKS);

	/*determine which data lost*/
	for (i = 0, nerrs = 0, nsrcerrs = 0; i < NDISKS  && nerrs < NDISKS -  NDATAS; i++) {
		j = atomic_read(IS_sess->cb_index_map + i );		
		if( IS_sess->cb_state_list[j] == CB_FAIL  || j < 0 ){ //failed slab and unmapped slab
		src_in_err[i] = 1;
		src_err_list[nerrs++] = i;
			if (i < NDATAS) {
				nsrcerrs++;
			}
		printk("****slab %d cb %d failed, nerrs %d nsrcerrs %d", i, j, nerrs, nsrcerrs);
		}
	}

	ret = gf_gen_decode_matrix(encode_matrix, decode_matrix,
				  invert_matrix, decode_index, src_err_list, src_in_err,
				  nerrs, nsrcerrs, NDATAS, NDISKS);
	
	if (ret != 0) {
		printk("Fail to gf_gen_decode_matrix\n");
		return -1;
	}

	/* construct decoding table per disconnection*/
	ec_init_tables(NDATAS, nerrs, decode_matrix, decode_tbls);


	//disallow request to those cb chunks 
	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		sess_chunk_index = cb_chunk_map[i];
		if (sess_chunk_index != -1) { //this cb chunk is mapped
			evict_list[sess_chunk_index] = 1;
			atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_UNMAPPED);
			atomic_set(IS_sess->cb_index_map + (sess_chunk_index), NO_CB_MAPPED); 
			pr_debug("%s, unmap chunk %d\n", __func__, sess_chunk_index);
		}
	}	

	pr_debug("%s, unmap %d GB in cb%d \n", __func__, cb->remote_chunk.chunk_size_g, pool_index);
	cb->remote_chunk.chunk_size_g = 0;



	for (i=0; i < submit_queues; i++){
		ctx_pool = IS_sess->IS_conns[i]->ctx_pools[pool_index]->ctx_pool;
		for (j=0; j < IS_QUEUE_DEPTH; j++){
			ctx = ctx_pool + j;
			switch (atomic_read(&ctx->in_flight)){
				case CTX_R_IN_FLIGHT:					
					req = ctx->req;
					IS_insert_ctx(ctx);
					//retry
					err = IS_request(ctx->req, IS_sess->xdev->queues );
					if ( unlikely(err) )
        				{
				                pr_err("transfer failed for req %p\n", req);
					}						
					break;
				case CTX_W_IN_FLIGHT:
					req = ctx->req;
					IS_insert_ctx(ctx);// don't need retry
					break;
				default:;
			}
		}
	}	
	pr_err("%s, finish handling in-flight request\n", __func__);
	pr_err("%s disconnected %s\n", IS_sess->portal_list[cb->cb_index].addr, IS_sess->xdev->file_name);



	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		sess_chunk_index = cb_chunk_map[i];
		if (sess_chunk_index != -1) { 
			IS_sess->chunk_map_cb_chunk[sess_chunk_index] = -1;
			IS_sess->free_chunk_index += 1;
			IS_sess->unmapped_chunk_list[IS_sess->free_chunk_index] = sess_chunk_index;
			cb_chunk_map[i] = -1;
		}
	}
	
	/*for (i=0; i<DISKSIZE_G; i++){
		if (evict_list[i] == 1){
			IS_single_chunk_map(IS_sess, i); //remapping?
		}
	}*/

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
	int evict_list[DISKSIZE_G]; //session chunk index

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
		for (i=0; i<DISKSIZE_G; i++){
			evict_list[i] = -1;	
		}
		for (i = 0; i < MAX_MR_SIZE_GB; i++) {
			cb->send_buf.rkey[i] = 0;
		}
		j = 0;

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
				atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_UNMAPPED);
			}
		}
		
		IS_sess->mapped_cb_num -= size_g;
		cb->remote_chunk.chunk_size_g -= size_g;
		cb->remote_chunk.shrink_size_g = 0;
		IS_send_done(cb, size_g);	

		cb->remote_chunk.c_state = C_READY;
		IS_sess->cb_state_list[cb->cb_index] = CB_EVICTING;
		for (i=0; i<DISKSIZE_G; i++){
			if (evict_list[i] == 1){
				IS_sess->chunk_map_cb_chunk[i] = -1;
				IS_sess->free_chunk_index += 1;
				IS_sess->unmapped_chunk_list[IS_sess->free_chunk_index] = i;
				IS_single_chunk_map(IS_sess, i);
			}
		}	
		IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
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

	//DEBUG
	//show_rdma_info(&cb->recv_buf);

	
	switch(cb->recv_buf.type){
		
		case FREE_SIZE:
	//		printk("FREE_SIZE\n");	
			cb->remote_chunk.target_size_g = cb->recv_buf.size_gb;
			cb->state = FREE_MEM_RECV;	
			break;
		case INFO:
	//		printk("INFO\n");	
			cb->IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
			cb->state = WAIT_OPS;
			IS_chunk_list_init(cb);
			break;
		case INFO_SINGLE:
	//		printk("INFO_SINGLE\n");	
			cb->IS_sess->cb_state_list[cb->cb_index] = CB_MAPPED;
			cb->state = WAIT_OPS;
			IS_single_chunk_init(cb);
			break;
		case EVICT:
	//		printk("EVICT\n");	
			cb->state = RECV_EVICT;
			client_recv_evict(cb);
			break;
		case STOP:
	//		printk("STOP\n");	
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
	int i;
	struct rdma_ctx *ctx;
	struct request  *req;
	struct bio	*bio;
	

	ctx = (struct rdma_ctx *)ptr_from_uint64(wc->wr_id);
	req = ctx->req;
	bio = req->bio;

        atomic_set(&ctx->in_flight, CTX_IDLE);
        ctx->chunk_index = -1;
        ctx->chunk_ptr = NULL;
        ctx->req = NULL;

#ifdef REP

	if( atomic_dec_and_test(ctx->cnt) ){

	struct rdma_ctx* ctxs[NDISKS];



#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (bio_multiple_segments(bio)){
        /*read can have segments */
        struct bio_vec bvl;
        struct bvec_iter iter;
        u32  bio_offset= 0;

        bio_for_each_segment(bvl, bio, iter){
        BUG_ON(bvl.bv_len!= IS_PAGE_SIZE); //each bvec is page
        memcpy(page_address(bvl.bv_page),
               ctxs[i]->rdma_buf + bio_offset,
               IS_PAGE_SIZE);
        bio_offset += IS_PAGE_SIZE;
        }
        }
        else
#endif
	{
        u32  bio_offset;
        for (bio_offset=0; bio; bio = bio->bi_next, bio_offset+=IS_PAGE_SIZE){
        for (i=0; i<NDATAS; i++){
        memcpy( bio_data(bio)+ i*IS_PAGE_SIZE/NDATAS,
                ctxs[i]->rdma_buf + bio_offset/NDATAS,
                IS_PAGE_SIZE/NDATAS );
        }
        }
        }

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
                blk_mq_end_request(req, 0);
        #else
                blk_mq_end_io(req, 0);
        #endif

	for (i=0; i<NDISKS; i++){
	ctxs[i] = ctx->ctxs[i];
	}


	for (i=0; i<NDISKS; i++){
        IS_insert_ctx(ctxs[i]); //return ctxs
	}

	}
#endif


#ifdef EC
//	printk(" ctx_index %d\n%s", ctx->index, ctx->rdma_buf );

	if( atomic_dec_and_test( ctx->cnt ) ){

	struct rdma_ctx* ctxs[NDISKS];

	/*pointers for EC*/
	unsigned char *ptrs[NDISKS];
	unsigned char *data[NDATAS];
        unsigned char *recov[NDISKS-NDATAS];


	for (i=0; i<NDISKS; i++){
	ctxs[i] = ctx->ctxs[i];
	ptrs[i] = ctxs[i]->rdma_buf; //point parity 
	}

	/*	recovery	*/
	if (nsrcerrs > 0){
	
	for (i=0; i < nerrs; i++){ //nsrcerrs?{
		recov[i] = ptrs[src_err_list[i]];
		//printk("recovery target: err index: %d \n", src_err_list[i] );
	}

	// Pack recovery array as list of valid sources
	// Its order must be the same as the order
	// to generate matrix b in gf_gen_decode_matrix

	for (i=0; i < NDATAS; i++){ 
		data[i] = ptrs[ decode_index[i] ];
		//printk("recovery data: decode index: %d \n", decode_index[i] );
	}

	kernel_fpu_begin();
	ec_encode_data_avx( blk_rq_bytes(req)/NDATAS, NDATAS, nerrs, decode_tbls, data, recov );
	kernel_fpu_end();

	}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (bio_multiple_segments(bio)){
	/*read can have segments */
        struct bio_vec bvl;
        struct bvec_iter iter;
        u32  bio_offset= 0;

	bio_for_each_segment(bvl, bio, iter){
	BUG_ON(bvl.bv_len!= IS_PAGE_SIZE); //each bvec is page
	for(i=0; i<NDATAS; i++){
        memcpy(page_address(bvl.bv_page) + i*IS_PAGE_SIZE/NDATAS,
	       ctxs[i]->rdma_buf + bio_offset/NDATAS,
               IS_PAGE_SIZE/NDATAS);
        }
	bio_offset += IS_PAGE_SIZE;
        }
	}
	else
#endif
	{
        u32  bio_offset;
        for (bio_offset=0; bio; bio = bio->bi_next, bio_offset+=IS_PAGE_SIZE){
	for (i=0; i<NDATAS; i++){
        memcpy( bio_data(bio)+ i*IS_PAGE_SIZE/NDATAS,
		ctxs[i]->rdma_buf + bio_offset/NDATAS,
                IS_PAGE_SIZE/NDATAS );
	}
        }
        }

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
                blk_mq_end_request(req, 0);
        #else
                blk_mq_end_io(req, 0);
        #endif

	for (i=0; i<NDISKS; i++){
        IS_insert_ctx(ctxs[i]); //return ctxs
	}

	}	
#endif

        return 0;
}

static int client_write_done(struct kernel_cb * cb, struct ib_wc *wc)
{
	struct rdma_ctx *ctx;
	struct request *req;

	ctx = (struct rdma_ctx *)ptr_from_uint64(wc->wr_id);	
	req = ctx->req;
	atomic_set(&ctx->in_flight, CTX_IDLE);

	if( atomic_dec_and_test(ctx->cnt) ){


	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
		blk_mq_end_request(req, 0);
	#else
		blk_mq_end_io(req, 0);
	#endif
	}

	ctx->chunk_index = -1;
	ctx->chunk_ptr = NULL;
	ctx->req = NULL;
	IS_insert_ctx(ctx); 

	return 0;
}

static void rdma_cq_event_handler(struct ib_cq * cq, struct kernel_cb *cb)
{
	//struct kernel_cb *cb=ctx;
	struct ib_wc wc;
	struct ib_recv_wr * bad_wr;
	int ret;
	BUG_ON(cb->cq != cq);
	if (cb->state == ERROR) {
		printk(KERN_ERR PFX "cq completion in ERROR state\n");
		return;
	}
	//ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);

	do {
		ret = ib_poll_cq(cb->cq, 1, &wc);	

		if (cb->IS_sess->cb_state_list[cb->cb_index] == CB_FAIL)
					goto error;
	} while(ret == 0);

//	while ((ret = ib_poll_cq(cb->cq, 1, &wc)) == 1) {
		if (wc.status) {
			if (wc.status == IB_WC_WR_FLUSH_ERR) {
				pr_info("cq flushed\n");
				//continue;
				return;
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
//	}

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
		cb->dma_mr = cb->pd->device->get_dma_mr(cb->pd, IB_ACCESS_LOCAL_WRITE|
					   IB_ACCESS_REMOTE_READ|
				           IB_ACCESS_REMOTE_WRITE);

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
	init_attr.cap.max_send_wr = cb->txdepth;
	init_attr.cap.max_recv_wr = 10240;  
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IB_QPT_RC;
	init_attr.send_cq = cb->cq;
	init_attr.recv_cq = cb->cq;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;

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
	memset(&init_attr, 0, sizeof(init_attr));
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
	init_attr.cqe = cb->txdepth * 2;
	init_attr.comp_vector = 0;
	cb->cq = ib_create_cq(cm_id->device, NULL, NULL, cb, &init_attr);
#else
//	cb->cq = ib_create_cq(cm_id->device, rdma_cq_event_handler, NULL, cb, cb->txdepth * 2, 0);
	cb->cq = ib_create_cq(cm_id->device, NULL, NULL, cb, cb->txdepth * 2, 0);
#endif


	if (IS_ERR(cb->cq)) {
		printk(KERN_ERR PFX "ib_create_cq failed\n");
		ret = PTR_ERR(cb->cq);
		goto err1;
	}
	pr_info("created cq %p\n", cb->cq);

	//ret = ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);
	//if (ret) {
	//	printk(KERN_ERR PFX "ib_create_cq failed\n");
	//	goto err2;
	//}

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

static int rdma_trigger(void *data) //No disk rdma trigger
{
	struct IS_session *IS_sess = data;
	int i = 0;

	pr_info("%s\n", __func__);

	for (i=0; i< DISKSIZE_G; i+=NDISKS){ //Map NDISKS unit of slab every chunk_map
		while( IS_single_chunk_map(IS_sess, i) );		
	}
	return 0;
}

int IS_create_device(struct IS_session *IS_session,
					   const char *xdev_name, struct IS_file *IS_file)
{
	int retval;
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
	//pr_info(PFX "st_size = %lu\n", IS_file->stbuf.st_size);
	IS_session->xdev = IS_file;
	retval = IS_register_block_device(IS_file);
	if (retval) {
		pr_err("failed to register IS device %s ret=%d\n",
		       IS_file->file_name, retval);
		goto err_queues;
	}

	IS_set_device_state(IS_file, DEVICE_RUNNING);
	rdma_trigger(IS_session);

	add_disk(IS_file->disk);
	return 0;

err_queues:
	IS_destroy_queues(IS_file);
err:
	return retval;
}

void IS_destroy_device(struct IS_session *IS_session,
                         struct IS_file *IS_file)
{ 
	int i, cb_index;
	pr_info("%s\n", __func__);

	IS_set_device_state(IS_file, DEVICE_OFFLINE);

	/*Unmap slabs*/
	for (i=0; i<DISKSIZE_G; i++){
		cb_index = atomic_read(IS_session->cb_index_map + i);
		if (cb_index > NO_CB_MAPPED){		
		struct kernel_cb* cb = IS_session->cb_list[cb_index];
		IS_send_done(cb, 1);
		}
	}

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
	pr_info("%s\n", __func__);
	IS_conn->IS_sess = NULL;
	IS_conn->conn_th = NULL;

	kfree(IS_conn);
}

static int IS_ec_init(void){

	encode_matrix = kzalloc( NDISKS * NDATAS * sizeof(unsigned char), GFP_KERNEL);
	encode_tbls = kzalloc (  ( NDISKS-NDATAS)*NDATAS*32 * sizeof(unsigned char), GFP_KERNEL);
	// The matrix generated by gf_gen_cauchy1_matrix
	// is always invertable.
	gf_gen_cauchy1_matrix(encode_matrix, NDISKS, NDATAS);
	// Generate g_tbls from encode matrix encode_matrix
	ec_init_tables(NDATAS, NDISKS - NDATAS, &encode_matrix[NDATAS * NDATAS], encode_tbls);
	return 0;
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
	#ifdef EC
	IS_ec_init();
	#endif



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
	cb->size = IS_PAGE_SIZE/NDATAS * MAX_SGL_LEN; 
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

	k = j;  //available servers
	if (k < NDISKS) {
		pr_err("Available servers (%d) < NDISKS (%d)", k, NDISKS ); //Not available number of independent servers
		msleep(1000);
		return -1;
	}

	/*query free memory for selected servers*/

	for (i=0; i < k; i++){
		cb_index = selection[i];
		if (IS_session->cb_state_list[cb_index] == CB_FAIL){
			continue;	
		}
		tmp_cb = IS_session->cb_list[cb_index];
		if (IS_session->cb_state_list[cb_index] > CB_IDLE) {
			IS_send_query(tmp_cb);
			rdma_cq_event_handler(tmp_cb->cq, tmp_cb);
			//BUG_ON(tmp_cb->state != FREE_MEM_RECV);
			//wait_event_interruptible(tmp_cb->sem, tmp_cb->state == FREE_MEM_RECV);
			tmp_cb->state = AFTER_FREE_MEM;
			free_mem[i] = tmp_cb->remote_chunk.target_size_g;
			free_mem_sorted[i] = cb_index;
		}else { //CB_IDLE
			kernel_cb_init(tmp_cb, IS_session);
			rdma_connect_upper(tmp_cb);	
			rdma_connect_down(tmp_cb);
			rdma_cq_event_handler(tmp_cb->cq, tmp_cb);
			//BUG_ON(tmp_cb->state != FREE_MEM_RECV);
			//wait_event_interruptible(tmp_cb->sem, tmp_cb->state == FREE_MEM_RECV);
			tmp_cb->state = AFTER_FREE_MEM;
			IS_session->cb_state_list[cb_index] = CB_CONNECTED; //add CB_CONNECTED		
			free_mem[i] = tmp_cb->remote_chunk.target_size_g;
			free_mem_sorted[i] = cb_index;
		}
	}

	/*sort free memory received from servers*/
	for (i=0; i<NDISKS; i++){
	for (j=i+1; j<k; j++) {
		if (free_mem[i] < free_mem[j]) {
			free_mem[i] += free_mem[j];	
			free_mem[j] = free_mem[i] - free_mem[j];
			free_mem[i] = free_mem[i] - free_mem[j];
			free_mem_sorted[i] += free_mem_sorted[j];	
			free_mem_sorted[j] = free_mem_sorted[i] - free_mem_sorted[j];
			free_mem_sorted[i] = free_mem_sorted[i] - free_mem_sorted[j];
		}
	}
	}

	if (free_mem[NDISKS-1] == 0){
		pr_err("Available free mem = 0" );
		msleep(1000);
		return -1;
	}

	for (i=0; i<NDISKS; i++){
	cb_index = free_mem_sorted[i];
	pr_err("%s: to cb %d", __func__, cb_index );
	tmp_cb = IS_session->cb_list[cb_index];
	if (IS_session->cb_state_list[cb_index] == CB_CONNECTED){ 
		IS_session->mapped_cb_num += 1;
		IS_ctx_dma_setup(tmp_cb, IS_session, cb_index); 
		memset(name, '\0', 2);
		name[0] = (char)((cb_index/26) + 97);
		tmp_cb->remote_chunk.evict_handle_thread = kthread_create(evict_handler, tmp_cb, name);
		wake_up_process(tmp_cb->remote_chunk.evict_handle_thread);	
	}
	printk("chunk%d send bind to cb%d\n", need_chunk+i, cb_index);
	IS_send_bind_single(tmp_cb, need_chunk + i);
	rdma_cq_event_handler(tmp_cb->cq, tmp_cb);
	//BUG_ON(tmp_cb->state != WAIT_OPS);
	//wait_event_interruptible(tmp_cb->sem, tmp_cb->state == WAIT_OPS);
	}
	return 0; 
}

int IS_session_create(const char *portal, struct IS_session *IS_session)
{
	int i, j, ret;
	printk(KERN_ALERT "In IS_session_create() with portal: %s\n", portal);
	
	memcpy(IS_session->portal, portal, strlen(portal));
	pr_err("%s\n", IS_session->portal);
	portal_parser(IS_session);

	IS_session->capacity_g = DATASIZE_G; 
	IS_session->capacity = (unsigned long long)DATASIZE_G * ONE_GB;
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
	IS_session->cb_index_map = kzalloc(sizeof(atomic_t) * DISKSIZE_G, GFP_KERNEL);
	IS_session->chunk_map_cb_chunk = (int*)kzalloc(sizeof(int) * DISKSIZE_G, GFP_KERNEL);
	IS_session->unmapped_chunk_list = (int*)kzalloc(sizeof(int) * DISKSIZE_G, GFP_KERNEL);
	IS_session->free_chunk_index = DISKSIZE_G - 1;

	for (i = 0; i < DISKSIZE_G; i++){
		atomic_set(IS_session->cb_index_map + i, NO_CB_MAPPED);
		IS_session->unmapped_chunk_list[i] = IS_session->capacity_g-1-i;
		IS_session->chunk_map_cb_chunk[i] = -1;
	}

	//IS-connection
	IS_session->IS_conns = (struct IS_connection **)kzalloc(submit_queues * sizeof(*IS_session->IS_conns), GFP_KERNEL);
	if (!IS_session->IS_conns) {
		pr_err("failed to allocate IS connections array\n");
		ret = -ENOMEM;
		goto err_destroy_portal;
	}
	for (i = 0; i < submit_queues; i++) {
		ret = IS_create_conn(IS_session, i, &IS_session->IS_conns[i]);
		if (ret)
			goto err_destroy_conns;
	}

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
	int i;
	pr_info("%s\n", __func__);
	
	/*destroy connection*/
	for (i = 0; i < IS_session->cb_num; i++) {
                IS_destroy_conn(IS_session->IS_conns[i]);
                IS_session->IS_conns[i] = NULL;
        }	
	kfree(IS_session->IS_conns);

	mutex_lock(&g_lock);
	list_del(&IS_session->list);
	mutex_unlock(&g_lock);

	IS_destroy_session_devices(IS_session);
}

static int __init IS_init_module(void)
{
	pr_info("%s\n", __func__);
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
	pr_info("%s\n", __func__);

	unregister_blkdev(IS_major, "infiniswap");

	list_for_each_entry_safe(IS_session, tmp, &g_IS_sessions, list) {
		IS_session_destroy(IS_session);
	}

	IS_destroy_configfs_files();

}

module_init(IS_init_module);
module_exit(IS_cleanup_module);
