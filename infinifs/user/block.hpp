#ifndef __BLOCK_HPP__
#define __BLOCK_HPP__

#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "infinifs.hpp"

class Configuration {
public:
	explicit Configuration(std::string device,
			std::string dir,
			uint32_t blocks,
			uint32_t block_size) noexcept
		: m_device(device)
		, m_dir(dir)
		, m_device_blocks(blocks)
		, m_block_size(block_size)
		, m_inode_blocks(CountInodeBlocks())
	{ }

	std::string const & Device() const noexcept
	{ return m_device; }

	std::string const & SourceDir() const noexcept
	{ return m_dir; }

	uint32_t Blocks() const noexcept
	{ return m_device_blocks; }

	uint32_t InodeBlocks() const noexcept
	{ return m_inode_blocks; }

	uint32_t BlockSize() const noexcept
	{ return m_block_size; }

private:
	uint32_t CountInodeBlocks() const noexcept
	{
		static uint32_t const BytesPerInode = 16384u;

		uint32_t const bytes = Blocks() * BlockSize();
		uint32_t const inodes = bytes / BytesPerInode;
		uint32_t const in_block = BlockSize() /
						sizeof(struct infinifs_inode);

		return (inodes + in_block - 1) / in_block;
	}

	std::string	m_device;
	std::string	m_dir;
	uint32_t	m_device_blocks;
	uint32_t	m_block_size;
	uint32_t	m_inode_blocks;
};

using ConfigurationPtr = std::shared_ptr<Configuration>;
using ConfigurationConstPtr = std::shared_ptr<Configuration const>;


class Block {
public:
	explicit Block(ConfigurationConstPtr config, size_t no)
		: m_config(config)
		, m_number(no)
		, m_data(m_config->BlockSize(), 0)
	{ }

	Block(Block const &) = delete;
	Block & operator=(Block const &) = delete;

	Block(Block &&) = delete;
	Block & operator=(Block &&) = delete;

	size_t BlockNo() const noexcept
	{ return m_number; }

	uint8_t * Data() noexcept
	{ return m_data.data(); }

	uint8_t const * Data() const noexcept
	{ return m_data.data(); }

	size_t Size() const noexcept
	{ return m_config->BlockSize(); }

	std::ostream & Dump(std::ostream & out) const
	{ return out.write(reinterpret_cast<char const *>(Data()), Size()); }

	std::istream & Read(std::istream & in)
	{ return in.read(reinterpret_cast<char *>(Data()), Size()); }

private:
	ConfigurationConstPtr	m_config;
	size_t			m_number;
	std::vector<uint8_t>	m_data;
};

using BlockPtr = std::shared_ptr<Block>;
using BlockConstPtr = std::shared_ptr<Block const>;


class BlocksCache {
public:
	explicit BlocksCache(ConfigurationConstPtr config);
	~BlocksCache();

	ConfigurationConstPtr Config() const noexcept;
	BlockPtr GetBlock(size_t no);
	void Sync();

	BlocksCache(BlocksCache &&) = delete;
	BlocksCache & operator=(BlocksCache &&) = delete;

	BlocksCache(BlocksCache const &) = delete;
	BlocksCache & operator=(BlocksCache const &) = delete;

private:
	BlockPtr ReadBlock(std::istream &in, size_t no);
	void WriteBlock(std::ostream &out, BlockPtr block);

	ConfigurationConstPtr		m_config;
	std::map<size_t, BlockPtr>	m_cache;
};

#endif /*__BLOCK_HPP__*/
