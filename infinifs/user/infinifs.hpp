#ifndef __AUFS_HPP__
#define __AUFS_HPP__

#include <cassert>
#include <cstring>
#include <cstdint>

static uint32_t const AUFS_MAGIC = 0x13131313;
static uint32_t const AUFS_NAME_MAXLEN = 28;

struct infinifs_super_block {
	uint32_t	asb_magic;
	uint32_t	asb_block_size;
	uint32_t	asb_root_inode;
	uint32_t	asb_inode_blocks;
};

static inline uint32_t & ASB_MAGIC(struct infinifs_super_block *asb)
{ return asb->asb_magic; }

static inline uint32_t & ASB_BLOCK_SIZE(struct infinifs_super_block *asb)
{ return asb->asb_block_size; }

static inline uint32_t & ASB_ROOT_INODE(struct infinifs_super_block *asb)
{ return asb->asb_root_inode; }

static inline uint32_t & ASB_INODE_BLOCKS(struct infinifs_super_block *asb)
{ return asb->asb_inode_blocks; }


struct infinifs_inode {
	uint32_t	ai_first;
	uint32_t	ai_blocks;
	uint32_t	ai_size;
	uint32_t	ai_gid;
	uint32_t	ai_uid;
	uint32_t	ai_mode;
	uint64_t	ai_ctime;
};

static inline uint32_t & AI_FIRST_BLOCK(struct infinifs_inode *ai)
{ return ai->ai_first; }

static inline uint32_t & AI_BLOCKS(struct infinifs_inode *ai)
{ return ai->ai_blocks; }

static inline uint32_t & AI_SIZE(struct infinifs_inode *ai)
{ return ai->ai_size; }

static inline uint32_t & AI_GID(struct infinifs_inode *ai)
{ return ai->ai_gid; }

static inline uint32_t & AI_UID(struct infinifs_inode *ai)
{ return ai->ai_uid; }

static inline uint32_t & AI_MODE(struct infinifs_inode *ai)
{ return ai->ai_mode; }

static inline uint64_t & AI_CTIME(struct infinifs_inode *ai)
{ return ai->ai_ctime; }


struct infinifs_dir_entry {
	char 		ade_name[AUFS_NAME_MAXLEN];
	uint32_t	ade_inode;
};

static inline uint32_t & ADE_INODE(struct infinifs_dir_entry *ade)
{ return ade->ade_inode; }

static inline char const * ADE_NAME(struct infinifs_dir_entry const *ade)
{ return ade->ade_name; }

static inline char * ADE_NAME(struct infinifs_dir_entry *ade)
{ return ade->ade_name; }

#endif /*__AUFS_HPP__*/
