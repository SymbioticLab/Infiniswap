#ifndef __FORMAT_HPP__
#define __FORMAT_HPP__

#include "block.hpp"

class Inode {
public:
	explicit Inode(BlocksCache &cache, uint32_t no);

	uint32_t InodeNo() const noexcept;

	uint32_t FirstBlock() const noexcept;
	void SetFirstBlock(uint32_t block) noexcept;

	uint32_t BlocksCount() const noexcept;
	void SetBlocksCount(uint32_t count) noexcept;

	uint32_t Size() const noexcept;
	void SetSize(uint32_t size) noexcept;

	uint32_t Gid() const noexcept;
	void SetGid(uint32_t gid) noexcept;

	uint32_t Uid() const noexcept;
	void SetUid(uint32_t uid) noexcept;

	uint32_t Mode() const noexcept;
	void SetMode(uint32_t mode) noexcept;

	uint64_t CreateTime() const noexcept;

private:
	void FillInode(BlocksCache &cache);

	uint32_t		m_inode;
	BlockPtr		m_block;
	struct infinifs_inode *	m_raw;
};


class SuperBlock {
public:
	explicit SuperBlock(BlocksCache &cache);

	uint32_t AllocateInode() noexcept;
	uint32_t AllocateBlocks(size_t blocks) noexcept;
	void SetRootInode(uint32_t root) noexcept;

private:
	void FillSuper(BlocksCache &cache) noexcept;
	void FillBlockMap(BlocksCache &cache) noexcept;
	void FillInodeMap(BlocksCache &Cache) noexcept;

	BlockPtr	m_super_block;
	BlockPtr	m_block_map;
	BlockPtr	m_inode_map;
};

class Formatter {
public:
	Formatter(ConfigurationConstPtr config)
		: m_config(config)
		, m_cache(config)
		, m_super(m_cache)
	{ }

	void SetRootInode(Inode const &inode) noexcept;
	Inode MkDir(uint32_t entries);
	Inode MkFile(uint32_t size);

	uint32_t Write(Inode &inode, uint8_t const *data, uint32_t size);

	void AddChild(Inode &inode, char const *name, Inode const &ch);

private:
	ConfigurationConstPtr	m_config;
	BlocksCache		m_cache;
	SuperBlock		m_super;
};

#endif /*__FORMAT_HPP__*/
