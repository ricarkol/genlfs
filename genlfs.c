#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "lfs.h"
#include "config.h"

static next_inum = 4;

int get_next_inum(void)
{
	return ++next_inum;
}

/*
   DT_BLK      This is a block device.
   DT_CHR      This is a character device.
   DT_DIR      This is a directory.
   DT_FIFO     This is a named pipe (FIFO).
   DT_LNK      This is a symbolic link.
   DT_REG      This is a regular file.
   DT_SOCK     This is a UNIX domain socket.
   DT_UNKNOWN  The file type is unknown.
   */

void walk(struct fs *fs, int parent_inum, int inum)
{
	DIR *d;
	struct dirent *dirent;
	struct directory dir = {0};
	char block[DFL_LFSBLOCK];

	d = opendir(".");

	if (d == NULL) {
		return;
	}

	while ((dirent = readdir(d)) != NULL) {
		struct stat sb;
		int next_inum = get_next_inum();

		printf("%d %s\n", dirent->d_ino, dirent->d_name);
		lstat(dirent->d_name, &sb);

		switch (sb.st_mode & S_IFMT) {
			case S_IFBLK:  printf("block device\n");            break;
			case S_IFCHR:  printf("character device\n");        break;
			case S_IFDIR: 
				       if (strcmp(dirent->d_name, ".") == 0)
					       break;
				       if (strcmp(dirent->d_name, "..") == 0)
					       break;
				       dir_add_entry(&dir, dirent->d_name,
						       next_inum, LFS_DT_DIR);
				       printf("directory\n");
				       chdir(dirent->d_name);
				       walk(fs, inum, next_inum);
				       chdir("..");
				       break;
			case S_IFIFO:  printf("FIFO/pipe\n");               break;
			case S_IFLNK:  printf("symlink\n");                 break;
			case S_IFREG:  printf("regular file\n");
					sprintf(block, "bla bla\n");
					write_file(fs, block, strlen(block),
						next_inum, LFS_IFREG | 0777, 1, 0);
				       dir_add_entry(&dir, dirent->d_name,
						       next_inum, LFS_DT_REG);
				       break;
			case S_IFSOCK: printf("socket\n");                  break;
			default:       printf("unknown?\n");                break;
		}

	}

	dir_add_entry(&dir, ".", inum, LFS_DT_DIR);
	dir_add_entry(&dir, "..", parent_inum, LFS_DT_DIR);
	dir_done(&dir);
	write_file(fs, &dir.data[0], 512, inum,	LFS_IFDIR | 0755, inum, 0);

	closedir(d);
}

int main(int argc, char **argv)
{
	struct fs fs;
	uint64_t nbytes = 1024 * 1024 * 1024 * 40ULL;

	if (argc != 3) {
		errx(1, "Usage: %s <directory> <image>", argv[0]);
	}

	fs.fd = open(argv[2], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fs.fd != 0);

	init_lfs(&fs, nbytes);

	chdir(argv[1]);
	walk(&fs, ULFS_ROOTINO, ULFS_ROOTINO);

	write_ifile(&fs);
	write_superblock(&fs);
	write_segment_summary(&fs);
	close(fs.fd);
}
