/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 *  
 * Stackbd
 * Copyright 2014 Oren Kishon
 * https://github.com/OrenKishon/stackbd
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static struct blk_mq_hw_ctx *IS_alloc_hctx(struct blk_mq_reg *reg,
					     unsigned int hctx_index)
{
	int b_size = DIV_ROUND_UP(reg->nr_hw_queues, nr_online_nodes); 
	int tip = (reg->nr_hw_queues % nr_online_nodes);
	int node = 0, i, n;
	struct blk_mq_hw_ctx * hctx;

	pr_info("hctx_index=%u, b_size=%d, tip=%d, nr_online_nodes=%d\n",
		 hctx_index, b_size, tip, nr_online_nodes);
	/*
	 * Split submit queues evenly wrt to the number of nodes. If uneven,
	 * fill the first buckets with one extra, until the rest is filled with
	 * no extra.
	 */
	for (i = 0, n = 1; i < hctx_index; i++, n++) {
		if (n % b_size == 0) {
			n = 0;
			node++;

			tip--;
			if (!tip)
				b_size = reg->nr_hw_queues / nr_online_nodes;
		}
	}

	/*
	 * A node might not be online, therefore map the relative node id to the
	 * real node id.
	 */
	for_each_online_node(n) {
		if (!node)
			break;
		node--;
	}
	pr_debug("%s: n=%d\n", __func__, n);
	hctx = kzalloc_node(sizeof(struct blk_mq_hw_ctx), GFP_KERNEL, n);

	return hctx;
}

static void IS_free_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_index)
{
	pr_info("%s called\n", __func__);
	kfree(hctx);
}
#endif

int IS_request(struct request *req, struct IS_queue *xq)
{
	struct IS_file *xdev = req->rq_disk->private_data;
        int write = rq_data_dir(req) == WRITE;
        unsigned long start = blk_rq_pos(req) << IS_SECT_SHIFT;
        unsigned long len  = blk_rq_bytes(req);
	struct IS_session *IS_sess = xq->IS_conn->IS_sess;
	int slab_index;
	unsigned long chunk_offset;
	int ret = 0;
	int cb_index[NDISKS];	
	int chunk_index[NDISKS];
	struct kernel_cb* cb[NDISKS];
	struct remote_chunk_g* chunk[NDISKS];
	int i;

	slab_index = start >> (SLAB_SHIFT);

	for(i=0; i<NDISKS; i++){
	cb_index[i] = atomic_read(IS_sess->cb_index_map + slab_index*NDISKS + i );

	if (cb_index[i] != NO_CB_MAPPED){
		//pr_err("%s:%d no cb to write\n", __FILE__, __LINE__ );
	//	return -1;
	//}

	cb[i] = IS_sess->cb_list[ cb_index[i] ];
	chunk_index[i] = IS_sess->chunk_map_cb_chunk[ slab_index*NDISKS + i];

	if (chunk_index[i] != -1){
	//	pr_err("%s:%d no chunk to write\n", __FILE__, __LINE__ );
	//	return -1;
	//}

	chunk[i] = cb[i]->remote_chunk.chunk_list[ chunk_index[i] ];
	}
	}
	}

	chunk_offset = (start & SLAB_MASK );


	ret = IS_transfer_chunk(xdev, cb, cb_index, chunk_index, chunk, chunk_offset, len, write, req, xq);

	if ( unlikely(ret) )
	{
		pr_err("transfer failed for req %p\n", req);
	}
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int IS_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 18, 0)
static int IS_queue_rq(struct blk_mq_hw_ctx *hctx, struct request *rq, bool last)
#else
static int IS_queue_rq(struct blk_mq_hw_ctx *hctx, struct request *rq)
#endif
{
	struct IS_queue *IS_q;
	int err;	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	struct request *rq = bd->rq;
#endif
	
	IS_q = hctx->driver_data; //get the queue from the hctx

	err = IS_request(rq, IS_q);

	if (unlikely(err)) {
		rq->errors = -EIO;
		return BLK_MQ_RQ_QUEUE_ERROR;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	blk_mq_start_request(rq);
#endif
	
	return BLK_MQ_RQ_QUEUE_OK;
}

// connect hctx with IS-file, IS-conn, and queue
static int IS_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int index)
{
	struct IS_file *xdev = data;
	struct IS_queue *xq;

	xq = &xdev->queues[index];
	pr_info("%s called index=%u xq=%p\n", __func__, index, xq);
	
	xq->IS_conn = xdev->IS_conns[index];
	xq->xdev = xdev;
	xq->queue_depth = xdev->queue_depth;
	hctx->driver_data = xq;

	return 0;
}

static struct blk_mq_ops IS_mq_ops = {
	.queue_rq       = IS_queue_rq,
	.map_queues      = blk_mq_map_queues,  
	.init_hctx	= IS_init_hctx,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
	.alloc_hctx	= IS_alloc_hctx,
	.free_hctx	= IS_free_hctx,
#endif
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static struct blk_mq_reg IS_mq_reg = {
	.ops		= &IS_mq_ops,
	.cmd_size	= sizeof(struct raio_io_u),
	.flags		= BLK_MQ_F_SHOULD_MERGE,
	.numa_node	= NUMA_NO_NODE,
	.queue_depth	= IS_QUEUE_DEPTH,
};
#endif

int IS_setup_queues(struct IS_file *xdev)
{
	pr_debug("%s called\n", __func__);
	xdev->queues = kzalloc(submit_queues * sizeof(*xdev->queues),
			GFP_KERNEL);
	if (!xdev->queues)
		return -ENOMEM;

	return 0;
}

static int IS_open(struct block_device *bd, fmode_t mode)
{
	pr_debug("%s called\n", __func__);
	return 0;
}

static void IS_release(struct gendisk *gd, fmode_t mode)
{
	pr_debug("%s called\n", __func__);
}

static int IS_media_changed(struct gendisk *gd)
{
	pr_debug("%s called\n", __func__);
	return 0;
}

static int IS_revalidate(struct gendisk *gd)
{
	pr_debug("%s called\n", __func__);
	return 0;
}

static int IS_ioctl(struct block_device *bd, fmode_t mode,
		      unsigned cmd, unsigned long arg)
{
	pr_debug("%s called\n", __func__);
	return -ENOTTY;
}

// bind to IS_file in IS_register_block_device
static struct block_device_operations IS_ops = {
	.owner           = THIS_MODULE,
	.open 	         = IS_open,
	.release 	 = IS_release,
	.media_changed   = IS_media_changed,
	.revalidate_disk = IS_revalidate,
	.ioctl	         = IS_ioctl
};

void IS_destroy_queues(struct IS_file *xdev)
{
	pr_info("%s\n", __func__);
	kfree(xdev->queues);
}

int IS_register_block_device(struct IS_file *IS_file)
{
	sector_t size = IS_file->stbuf.st_size;
	int page_size = PAGE_SIZE;
	int err = 0;

	pr_info("%s\n", __func__);
	IS_file->major = IS_major;

	// set device params 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
	IS_mq_reg.nr_hw_queues = submit_queues;
	IS_file->queue = blk_mq_init_queue(&IS_mq_reg, IS_file);  // IS_mq_req was defined above
#else
	IS_file->tag_set.ops = &IS_mq_ops;
	IS_file->tag_set.nr_hw_queues = submit_queues;
	IS_file->tag_set.queue_depth = IS_QUEUE_DEPTH;
	IS_file->tag_set.numa_node = NUMA_NO_NODE;
	IS_file->tag_set.cmd_size	= sizeof(struct raio_io_u); // this may need chagne
	IS_file->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	IS_file->tag_set.driver_data = IS_file;

	err = blk_mq_alloc_tag_set(&IS_file->tag_set);
	if (err)
		return err;

	IS_file->queue = blk_mq_init_queue(&IS_file->tag_set);
#endif
	if (IS_ERR(IS_file->queue)) {
		pr_err("%s: Failed to allocate blk queue ret=%ld\n",
		       __func__, PTR_ERR(IS_file->queue));
		err = PTR_ERR(IS_file->queue);
		goto blk_mq_init;
	}

	IS_file->queue->queuedata = IS_file;
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, IS_file->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, IS_file->queue);

	IS_file->disk = alloc_disk_node(1, NUMA_NO_NODE);
	if (!IS_file->disk) {
		pr_err("%s: Failed to allocate disk node\n", __func__);
		err = -ENOMEM;
		goto alloc_disk;
	}

	// device setting info, kernel may make swap based on this info
	IS_file->disk->major = IS_file->major;
	IS_file->disk->first_minor = IS_file->index;
	IS_file->disk->fops = &IS_ops;	// pay attention to IS_ops
	IS_file->disk->queue = IS_file->queue;
	IS_file->disk->private_data = IS_file;
	blk_queue_logical_block_size(IS_file->queue, IS_SECT_SIZE); //block size = 512
	blk_queue_physical_block_size(IS_file->queue, IS_SECT_SIZE);
	sector_div(page_size, IS_SECT_SIZE);
	blk_queue_max_hw_sectors(IS_file->queue, page_size * MAX_SGL_LEN);
	sector_div(size, IS_SECT_SIZE);
	set_capacity(IS_file->disk, size);  // size is in remote file state->size, add size info into block device
	sscanf(IS_file->dev_name, "%s", IS_file->disk->disk_name);
	pr_err("%s, dev_name %s\n", __func__, IS_file->dev_name);
        printk("IS: init done\n");

	return err;

alloc_disk:
	blk_cleanup_queue(IS_file->queue);
blk_mq_init:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	blk_mq_free_tag_set(&IS_file->tag_set);
#endif
	return err;
}

void IS_unregister_block_device(struct IS_file *IS_file)
{
	del_gendisk(IS_file->disk);
	blk_cleanup_queue(IS_file->queue);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	blk_mq_free_tag_set(&IS_file->tag_set);
#endif
	put_disk(IS_file->disk);
}

void IS_single_chunk_init(struct kernel_cb *cb)
{
	int i = 0;
	int select_chunk = cb->recv_buf.size_gb;
	struct IS_session *IS_session = cb->IS_sess;

	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		if (cb->recv_buf.rkey[i]) { //from server, this chunk is allocated and given to you
			pr_info("Received rkey %x addr %llx from peer\n", ntohl(cb->recv_buf.rkey[i]), (unsigned long long)ntohll(cb->recv_buf.buf[i]));	
			cb->remote_chunk.chunk_list[i]->remote_rkey = ntohl(cb->recv_buf.rkey[i]);
			cb->remote_chunk.chunk_list[i]->remote_addr = ntohll(cb->recv_buf.buf[i]);
			cb->remote_chunk.chunk_list[i]->bitmap_g = (int *)kzalloc(sizeof(int) * BITMAP_INT_SIZE, GFP_KERNEL);
			IS_bitmap_init(cb->remote_chunk.chunk_list[i]->bitmap_g);
			IS_session->free_chunk_index -= 1;
			IS_session->chunk_map_cb_chunk[select_chunk] = i;
			cb->remote_chunk.chunk_map[i] = select_chunk;

			cb->remote_chunk.chunk_size_g += 1;
			cb->remote_chunk.c_state = C_READY;
			atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_MAPPED);
			atomic_set(IS_session->cb_index_map + (select_chunk), cb->cb_index);
			break;
		}
	}
}

void IS_chunk_list_init(struct kernel_cb *cb)
{
	int i = 0;
	int size_g = cb->recv_buf.size_gb;
	struct IS_session *IS_session = cb->IS_sess;
	int sess_free_chunk;
	int j = 0;

	for (i = 0; i < MAX_MR_SIZE_GB; i++) {
		if (cb->recv_buf.rkey[i]) { 
			pr_info("Received rkey %x addr %llx from peer\n", ntohl(cb->recv_buf.rkey[i]), (unsigned long long)ntohll(cb->recv_buf.buf[i]));	
			cb->remote_chunk.chunk_list[i]->remote_rkey = ntohl(cb->recv_buf.rkey[i]);
			cb->remote_chunk.chunk_list[i]->remote_addr = ntohll(cb->recv_buf.buf[i]);
			cb->remote_chunk.chunk_list[i]->bitmap_g = (int *)kzalloc(sizeof(int) * BITMAP_INT_SIZE, GFP_KERNEL);
			IS_bitmap_init(cb->remote_chunk.chunk_list[i]->bitmap_g);
			atomic_set(cb->remote_chunk.remote_mapped + i, CHUNK_MAPPED);
			sess_free_chunk = IS_session->unmapped_chunk_list[IS_session->free_chunk_index];
			IS_session->free_chunk_index -= 1;
			atomic_set(IS_session->cb_index_map + (sess_free_chunk), cb->cb_index);
			IS_session->chunk_map_cb_chunk[sess_free_chunk] = i;
			cb->remote_chunk.chunk_map[i] = sess_free_chunk;
			j += 1;
		}
	}
	if (j != size_g){
		pr_err("%s, j%d != size_g%d\n", __func__, j, size_g);
	}
	cb->remote_chunk.chunk_size_g += size_g;
	cb->remote_chunk.c_state = C_READY;
}


void IS_bitmap_set(int *bitmap, int i)
{
	bitmap[i >> BITMAP_SHIFT] |= 1 << (i & BITMAP_MASK);
}

void IS_bitmap_group_set(int *bitmap, unsigned long offset, unsigned long len)
{
	int start_page = (int)(offset/IS_PAGE_SIZE);	
	int len_page = (int)(len/IS_PAGE_SIZE);
	int i;
	for (i=0; i<len_page; i++){
		IS_bitmap_set(bitmap, start_page + i);
	}
}
void IS_bitmap_group_clear(int *bitmap, unsigned long offset, unsigned long len)
{
	int start_page = (int)(offset/IS_PAGE_SIZE);	
	int len_page = (int)(len/IS_PAGE_SIZE);
	int i;
	for (i=0; i<len_page; i++){
		IS_bitmap_clear(bitmap, start_page + i);
	}
}
bool IS_bitmap_test(int *bitmap, int i)
{
	if ((bitmap[i >> BITMAP_SHIFT] & (1 << (i & BITMAP_MASK))) != 0){
		return true;
	}else{
		return false;
	}
}


void IS_bitmap_clear(int *bitmap, int i)
{
	bitmap[i >> BITMAP_SHIFT] &= ~(1 << (i & BITMAP_MASK));
}
void IS_bitmap_init(int *bitmap)
{
	memset(bitmap, 0x00, ONE_GB/(4096*8));
}
