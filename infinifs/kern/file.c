#include <linux/fs.h>

const struct file_operations infinifs_file_ops = {
	.llseek = generic_file_llseek,
//	.read = new_sync_read,
	.read_iter = generic_file_read_iter,
	.mmap = generic_file_mmap,
	.splice_read = generic_file_splice_read
};
