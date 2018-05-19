#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pagemap.h>

#include "infinifs.h"

static size_t infinifs_dir_offset(size_t idx)
{
	return idx * sizeof(struct infinifs_disk_dir_entry);
}

static size_t infinifs_dir_pages(struct inode *inode)
{
	size_t size = infinifs_dir_offset(inode->i_size);

	return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static size_t infinifs_dir_entry_page(size_t idx)
{
	return infinifs_dir_offset(idx) >> PAGE_SHIFT;
}

static inline size_t infinifs_dir_entry_offset(size_t idx)
{
	return infinifs_dir_offset(idx) -
			(infinifs_dir_entry_page(idx) << PAGE_SHIFT);
}

static struct page *infinifs_get_page(struct inode *inode, size_t n)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);

	if (!IS_ERR(page))
		kmap(page);
	return page;
}

static void infinifs_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}

static int infinifs_dir_emit(struct dir_context *ctx,
			struct infinifs_disk_dir_entry *de)
{
	unsigned type = DT_UNKNOWN;
	unsigned len = strlen(de->dde_name);
	size_t ino = be32_to_cpu(de->dde_inode);

	return dir_emit(ctx, de->dde_name, len, ino, type);
}

static int infinifs_iterate(struct inode *inode, struct dir_context *ctx)
{
	size_t pages = infinifs_dir_pages(inode);
	size_t pidx = infinifs_dir_entry_page(ctx->pos);
	size_t off = infinifs_dir_entry_offset(ctx->pos);

	for ( ; pidx < pages; ++pidx, off = 0) {
		struct page *page = infinifs_get_page(inode, pidx);
		struct infinifs_disk_dir_entry *de;
		char *kaddr;

		if (IS_ERR(page)) {
			pr_err("cannot access page %u in %lu", pidx,
						(unsigned long)inode->i_ino);
			return PTR_ERR(page);
		}

		kaddr = page_address(page);
		de = (struct infinifs_disk_dir_entry *)(kaddr + off);
		while (off < PAGE_SIZE && ctx->pos < inode->i_size) {
			if (!infinifs_dir_emit(ctx, de)) {
				infinifs_put_page(page);
				return 0;
			}
			++ctx->pos;
			++de;
		}
		infinifs_put_page(page);
	}
	return 0;
}

static int infinifs_readdir(struct file *file, struct dir_context *ctx)
{
	return infinifs_iterate(file_inode(file), ctx);
}

const struct file_operations infinifs_dir_ops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate = infinifs_readdir,
};

struct infinifs_filename_match {
	struct dir_context ctx;
	ino_t ino;
	const char *name;
	int len;
};

static int infinifs_match(struct dir_context *ctx, const char *name, int len,
			loff_t off, u64 ino, unsigned type)
{
	struct infinifs_filename_match *match = (struct infinifs_filename_match *)ctx;

	if (len != match->len)
		return 0;

	if (memcmp(match->name, name, len) == 0) {
		match->ino = ino;
		return 1;
	}
	return 0;
}

static ino_t infinifs_inode_by_name(struct inode *dir, struct qstr *child)
{
	struct infinifs_filename_match match = {
		{ &infinifs_match, 0 }, 0, child->name, child->len
	};

	int err = infinifs_iterate(dir, &match.ctx);

	if (err)
		pr_err("Cannot find dir entry, error = %d", err);
	return match.ino;
}

static struct dentry *infinifs_lookup(struct inode *dir, struct dentry *dentry,
			unsigned flags)
{
	struct inode *inode = NULL;
	ino_t ino;

	if (dentry->d_name.len >= INFINIFS_DDE_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = infinifs_inode_by_name(dir, &dentry->d_name);
	if (ino) {
		inode = infinifs_inode_get(dir->i_sb, ino);
		if (IS_ERR(inode)) {
			pr_err("Cannot read inode %lu", (unsigned long)ino);
			return ERR_PTR(PTR_ERR(inode));
		}
		d_add(dentry, inode);
	}
	return NULL;
}

const struct inode_operations infinifs_dir_inode_ops = {
	.lookup = infinifs_lookup,
};
