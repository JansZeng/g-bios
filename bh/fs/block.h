#pragma once

#include <linux/types.h>

#define DISK_BLOCK_SIZE	 512

struct block_device
{
	const char *name;
	int fd;
};

struct block_device *bdev_open(const char *name);

int bdev_close(struct block_device *bdev);

// ssize_t bdev_read_block(struct block_device *bdev, int blk_no, void *buff);

ssize_t bdev_read_block(struct block_device *bdev, int blk_no, size_t off, void *buff, size_t size);
