#include <fstream>
#include <iostream>

#include "block.hpp"

BlocksCache::BlocksCache(ConfigurationConstPtr config)
	: m_config(config)
{ }

BlocksCache::~BlocksCache()
{
	try {
		Sync();
	} catch (...) {
		std::cout << "PANIC: Cannot sync blocks with device"
			<< std::endl;
	}
}

ConfigurationConstPtr BlocksCache::Config() const noexcept
{ return m_config; }

BlockPtr BlocksCache::GetBlock(size_t no)
{
	static size_t const ThresholdSize = 1048576u;

	std::map<size_t, BlockPtr>::iterator it(m_cache.find(no));
	if (it != std::end(m_cache))
		return it->second;

	if (Config()->BlockSize() * m_cache.size() > ThresholdSize)
		Sync();

	std::ifstream in(Config()->Device().c_str(),
		std::ios::in | std::ios::binary);

	BlockPtr block = ReadBlock(in, no);
	m_cache.insert(std::make_pair(no, block));

	return block;
}

void BlocksCache::Sync()
{
	std::fstream out(Config()->Device().c_str(),
		std::ios::out | std::ios::in | std::ios::binary);

	std::map<size_t, BlockPtr>::iterator it(std::begin(m_cache));
	std::map<size_t, BlockPtr>::iterator const e(std::end(m_cache));
	while (it != e) {
		WriteBlock(out, it->second);
		if (it->second.unique())
			it = m_cache.erase(it);
		else
			++it;
	}
}

BlockPtr BlocksCache::ReadBlock(std::istream &in, size_t no)
{
	BlockPtr block = std::make_shared<Block>(Config(), no);
	block->Read(in.seekg(no * Config()->BlockSize()));

	return block;
}

void BlocksCache::WriteBlock(std::ostream &out, BlockPtr block)
{ block->Dump(out.seekp(block->BlockNo() * block->Size())); }
