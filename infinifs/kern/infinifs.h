#ifndef __INFINIFS_H__
#define __INFINIFS_H__

#include <linux/types.h>
#include <linux/fs.h>

#define INFINIFS_DDE_SIZE         32
#define INFINIFS_DDE_MAX_NAME_LEN (INFINIFS_DDE_SIZE - sizeof(__be32))

static const unsigned long INFINIFS_MAGIC = 0x13131313;

struct infinifs_disk_super_block {
	__be32	dsb_magic;
	__be32	dsb_block_size;
	__be32	dsb_root_inode;
	__be32	dsb_inode_blocks;
};

struct infinifs_disk_inode {
	__be32	di_first;
	__be32	di_blocks;
	__be32	di_size;
	__be32	di_gid;
	__be32	di_uid;
	__be32	di_mode;
	__be64	di_ctime;
};

struct infinifs_disk_dir_entry {
	char dde_name[INFINIFS_DDE_MAX_NAME_LEN];
	__be32 dde_inode;
};

struct infinifs_super_block {
	unsigned long asb_magic;
	unsigned long asb_inode_blocks;
	unsigned long asb_block_size;
	unsigned long asb_root_inode;
	unsigned long asb_inodes_in_block;
};

static inline struct infinifs_super_block *INFINIFS_SB(struct super_block *sb)
{
	return (struct infinifs_super_block *)sb->s_fs_info;
}

struct infinifs_inode {
	struct inode ai_inode;
	unsigned long ai_block;
};

static inline struct infinifs_inode *INFINIFS_INODE(struct inode *inode)
{
	return (struct infinifs_inode *)inode;
}

extern const struct address_space_operations infinifs_aops;
extern const struct inode_operations infinifs_dir_inode_ops;
extern const struct file_operations infinifs_file_ops;
extern const struct file_operations infinifs_dir_ops;

struct inode *infinifs_inode_get(struct super_block *sb, unsigned long no);
struct inode *infinifs_inode_alloc(struct super_block *sb);
void infinifs_inode_free(struct inode *inode);

#endif /*__INFINIFS_H__*/
