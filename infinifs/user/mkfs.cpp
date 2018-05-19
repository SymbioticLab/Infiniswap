#include <algorithm>
#include <iostream>
#include <iterator>
#include <fstream>
#include <vector>
#include <string>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "format.hpp"

size_t DeviceSize(std::string const & device)
{
	std::ifstream in(device.c_str(),
		std::ios::in | std::ios::binary | std::ios::ate);
	std::ifstream::streampos const size = in.tellg();

	return static_cast<size_t>(size);
}

bool VerifyBlocks(ConfigurationConstPtr config)
{
	if (config->Blocks() <= 3)
		return false;

	return true;
}

bool VerifyDevice(ConfigurationConstPtr config)
{
	size_t const size = DeviceSize(config->Device());

	if (size < config->Blocks() * config->BlockSize())
		return false;

	return true;
}

bool VerifyBlockSize(ConfigurationConstPtr config)
{
	if (config->BlockSize() != 512u && config->BlockSize() != 1024u &&
		config->BlockSize() != 2048u && config->BlockSize() != 4096u)
		return false;

	if (config->BlockSize() * 8 < config->Blocks())
		std::cout << "WARNING: With block size = "
			<< config->BlockSize() << " blocks number should be "
			<< "less or equal to " << config->BlockSize() * 8
			<< std::endl;

	return true;
}

ConfigurationConstPtr VerifyConfiguration(ConfigurationConstPtr config)
{
	if (!VerifyBlockSize(config))
		throw std::runtime_error("Unsupported block size");

	if (!VerifyBlocks(config))
		throw std::runtime_error("Wrong number of blocks");

	if (!VerifyDevice(config))
		throw std::runtime_error("Device is too small");

	return config;
}

void PrintHelp()
{
	std::cout << "Usage:" << std::endl
		<< "\tmkfs.aufs [(--block_size | -s) SIZE] [(--blocks | -b) BLOCKS] DEVICE"
		<< std::endl << std::endl
		<< "Where:" << std::endl
		<< "\tSIZE    - block size. Default is 4096 bytes." << std::endl
		<< "\tBLOCKS  - number of blocks would be used for aufs. By default is DEVICE size / SIZE." << std::endl
		<< "\tDEVICE  - device file." << std::endl;
}

ConfigurationConstPtr ParseArgs(int argc, char **argv)
{
	std::string device, dir;
	size_t block_size = 4096u;
	size_t blocks = 0;

	while (argc--) {
		std::string const arg(*argv++);
		if ((arg == "--blocks" || arg == "-b") && argc) {
			blocks = std::stoi(*argv++);
			--argc;
		} else if ((arg == "--block_size" || arg == "-s") && argc) {
			block_size = std::stoi(*argv++);
			--argc;
		} else if ((arg == "--dir" || arg == "-d") && argc) {
			dir = *argv++;
			--argc;
		} else if (arg == "--help" || arg == "-h") {
			PrintHelp();
		} else {
			device = arg;
		}
	}

	if (device.empty())
		throw std::runtime_error("Device name expected");

	if (blocks == 0)
		blocks = std::min(DeviceSize(device) / block_size, block_size * 8);

	ConfigurationConstPtr config = std::make_shared<Configuration>(
		device, dir, blocks, block_size);

	return VerifyConfiguration(config);
}

Inode CopyFile(Formatter &fmt, std::string const &path)
{
	std::vector<char> data;
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::runtime_error("cannot open file");
	std::istream_iterator<char> b(file), e;
	std::copy(b, e, std::back_inserter(data));

	Inode inode = fmt.MkFile(data.size());
	size_t written = 0;
	while (written != data.size()) {
		written += fmt.Write(inode,
			reinterpret_cast<uint8_t const *>(data.data()) +
				written,
			data.size() - written);
	}

	return inode;
}

Inode CopyDir(Formatter &fmt, std::string const &path)
{
	std::vector<std::string> entries;
	struct dirent entry, *entryp = &entry;
	std::unique_ptr<DIR, int(*)(DIR *)> dirp(opendir(path.c_str()),
							&closedir);
	if (!dirp.get())
		throw std::runtime_error("cannot open dir");

	while (entryp) {
		readdir_r(dirp.get(), &entry, &entryp);
		if (entryp && strcmp(entry.d_name, ".")
				&& strcmp(entry.d_name, ".."))
			entries.push_back(std::string(entry.d_name)
					.substr(0, AUFS_NAME_MAXLEN - 1));
	}

	Inode inode = fmt.MkDir(entries.size());
	for (std::string const &entry : entries) {
		struct stat buffer;
		if (stat((path + "/" + entry).c_str(), &buffer))
			continue;
		if (buffer.st_mode & S_IFDIR)
			fmt.AddChild(inode, entry.c_str(),
					CopyDir(fmt, path + "/" + entry));
		else
			fmt.AddChild(inode, entry.c_str(),
					CopyFile(fmt, path + "/" + entry));
	}

	return inode;
}

int main(int argc, char **argv)
{
	try {
		ConfigurationConstPtr config = ParseArgs(argc - 1, argv + 1);
		Formatter format(config);

		if (!config->SourceDir().empty())
			format.SetRootInode(CopyDir(format,
						config->SourceDir()));
		else
			format.SetRootInode(format.MkDir(16));

		return 0;
	} catch (std::exception const & e) {
		std::cout << "ERROR: " << e.what() << std::endl;
		PrintHelp();
	}

	return 1;
}
