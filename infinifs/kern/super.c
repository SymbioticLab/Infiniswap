#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "infinifs.h"

static void infinifs_put_super(struct super_block *sb)
{
	struct infinifs_super_block *asb = INFINIFS_SB(sb);

	if (asb)
		kfree(asb);
	sb->s_fs_info = NULL;
	pr_debug("infinifs super block destroyed\n");
}

static struct super_operations const infinifs_super_ops = {
	.alloc_inode = infinifs_inode_alloc,
	.destroy_inode = infinifs_inode_free,
	.put_super = infinifs_put_super,
};

static inline void infinifs_super_block_fill(struct infinifs_super_block *asb,
			struct infinifs_disk_super_block const *dsb)
{
	asb->asb_magic = be32_to_cpu(dsb->dsb_magic);
	asb->asb_inode_blocks = be32_to_cpu(dsb->dsb_inode_blocks);
	asb->asb_block_size = be32_to_cpu(dsb->dsb_block_size);
	asb->asb_root_inode = be32_to_cpu(dsb->dsb_root_inode);
	asb->asb_inodes_in_block =
		asb->asb_block_size / sizeof(struct infinifs_disk_inode);
}

static struct infinifs_super_block *infinifs_super_block_read(struct super_block *sb)
{
	struct infinifs_super_block *asb = (struct infinifs_super_block *)
			kzalloc(sizeof(struct infinifs_super_block), GFP_NOFS);
	struct infinifs_disk_super_block *dsb;
	struct buffer_head *bh;

	if (!asb) {
		pr_err("infinifs cannot allocate super block\n");
		return NULL;
	}

	bh = sb_bread(sb, 0);
	if (!bh) {
		pr_err("cannot read 0 block\n");
		goto free_memory;
	}

	dsb = (struct infinifs_disk_super_block *)bh->b_data;
	infinifs_super_block_fill(asb, dsb);
	brelse(bh);

/*	if (asb->asb_magic != INFINIFS_MAGIC) {
		pr_err("wrong magic number %lu\n",
			(unsigned long)asb->asb_magic);
		goto free_memory;
	}*/

	pr_debug("infinifs super block info:\n"
		"\tmagic           = %lu\n"
		"\tinode blocks    = %lu\n"
		"\tblock size      = %lu\n"
		"\troot inode      = %lu\n"
		"\tinodes in block = %lu\n",
		(unsigned long)asb->asb_magic,
		(unsigned long)asb->asb_inode_blocks,
		(unsigned long)asb->asb_block_size,
		(unsigned long)asb->asb_root_inode,
		(unsigned long)asb->asb_inodes_in_block);

	return asb;

free_memory:
	kfree(asb);
	return NULL;
}

static int infinifs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct infinifs_super_block *asb = infinifs_super_block_read(sb);
	struct inode *root;

	if (!asb)
		return -EINVAL;

	sb->s_magic = asb->asb_magic;
	sb->s_fs_info = asb;
	sb->s_op = &infinifs_super_ops;

	if (sb_set_blocksize(sb, asb->asb_block_size) == 0) {
		pr_err("device does not support block size %lu\n",
			(unsigned long)asb->asb_block_size);
		return -EINVAL;
	}

	root = infinifs_inode_get(sb, asb->asb_root_inode);
	if (IS_ERR(root))
		return PTR_ERR(root);

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_err("infinifs cannot create root\n");
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *infinifs_mount(struct file_system_type *type, int flags,
			char const *dev, void *data)
{
	struct dentry *entry = mount_bdev(type, flags, dev, data, infinifs_fill_sb);

	if (IS_ERR(entry))
		pr_err("infinifs mounting failed\n");
	else
		pr_debug("infinifs mounted\n");
	return entry;
}

static struct file_system_type infinifs_type = {
	.owner = THIS_MODULE,
	.name = "infinifs",
	.mount = infinifs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
};

static struct kmem_cache *infinifs_inode_cache;

struct inode *infinifs_inode_alloc(struct super_block *sb)
{
	struct infinifs_inode *inode = (struct infinifs_inode *)
				kmem_cache_alloc(infinifs_inode_cache, GFP_KERNEL);

	if (!inode)
		return NULL;
	return &inode->ai_inode;
}

static void infinifs_free_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);

	pr_debug("freeing inode %lu\n", (unsigned long)inode->i_ino);
	kmem_cache_free(infinifs_inode_cache, INFINIFS_INODE(inode));
}

void infinifs_inode_free(struct inode *inode)
{
	call_rcu(&inode->i_rcu, infinifs_free_callback);
}

static void infinifs_inode_init_once(void *i)
{
	struct infinifs_inode *inode = (struct infinifs_inode *)i;

	inode_init_once(&inode->ai_inode);
}

static int infinifs_inode_cache_create(void)
{
	infinifs_inode_cache = kmem_cache_create("infinifs_inode",
		sizeof(struct infinifs_inode), 0,
		(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD), infinifs_inode_init_once);
	if (infinifs_inode_cache == NULL)
		return -ENOMEM;
	return 0;
}

static void infinifs_inode_cache_destroy(void)
{
	rcu_barrier();
	kmem_cache_destroy(infinifs_inode_cache);
	infinifs_inode_cache = NULL;
}

static int __init infinifs_init(void)
{
	int ret = infinifs_inode_cache_create();

	if (ret != 0) {
		pr_err("cannot create inode cache\n");
		return ret;
	}

	ret = register_filesystem(&infinifs_type);
	if (ret != 0) {
		infinifs_inode_cache_destroy();
		pr_err("cannot register filesystem\n");
		return ret;
	}

	pr_debug("infinifs module loaded\n");

	return 0;
}

static void __exit infinifs_fini(void)
{
	int ret = unregister_filesystem(&infinifs_type);

	if (ret != 0)
		pr_err("cannot unregister filesystem\n");

	infinifs_inode_cache_destroy();

	pr_debug("infinifs module unloaded\n");
}

module_init(infinifs_init);
module_exit(infinifs_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmu");
