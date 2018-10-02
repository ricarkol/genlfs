#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>

#include "config.h"
#include "lfs.h"

static next_inum = 4;

int get_next_inum(void) { return ++next_inum; }

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

void walk(struct fs *fs, int parent_inum, int inum) {
	DIR *d;
	struct dirent *dirent;
	struct directory *dir = calloc(1, sizeof(struct directory));
	assert(dir);

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
		case S_IFBLK:
			printf("block device\n");
			break;
		case S_IFCHR:
			printf("character device\n");
			break;
		case S_IFDIR:
			if (strcmp(dirent->d_name, ".") == 0)
				break;
			if (strcmp(dirent->d_name, "..") == 0)
				break;
			dir_add_entry(dir, dirent->d_name, next_inum,
				      LFS_DT_DIR);
			printf("directory\n");
			chdir(dirent->d_name);
			walk(fs, inum, next_inum);
			chdir("..");
			break;
		case S_IFIFO:
			printf("FIFO/pipe\n");
			break;
		case S_IFLNK:
			printf("symlink\n");
			break;
		case S_IFREG: {
			int fd = open(dirent->d_name, O_RDONLY);
			assert(fd);
			void *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			printf("regular file (%d): %s\n", fd, dirent->d_name);
			assert(addr);
			write_file(fs, (char *)addr, sb.st_size, next_inum,
					   LFS_IFREG | 0777, 1, 0);
			munmap(addr, sb.st_size);
			close(fd);

			dir_add_entry(dir, dirent->d_name, next_inum,
				      LFS_DT_REG);
			break;
		}
		case S_IFSOCK:
			printf("socket\n");
			break;
		default:
			printf("unknown?\n");
			break;
		}
	}

	dir_add_entry(dir, ".", inum, LFS_DT_DIR);
	dir_add_entry(dir, "..", parent_inum, LFS_DT_DIR);
	dir_done(dir);
	write_file(fs, dir->data, LFS_DIRBLKSIZ, inum, LFS_IFDIR | 0755, inum, 0);
	free(dir);

	closedir(d);
}

int main(int argc, char **argv) {
	struct fs fs;
	uint64_t nbytes = 1024 * 1024 * 1024 * 40ULL;

	if (argc != 3) {
		errx(1, "Usage: %s <directory> <image>", argv[0]);
	}

	fs.fd = open(argv[2], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fs.fd != 0);

	init_lfs(&fs, nbytes);

	if (chdir(argv[1]) != 0)
		return 1;

	walk(&fs, ULFS_ROOTINO, ULFS_ROOTINO);

	write_ifile(&fs);
	write_superblock(&fs);
	write_segment_summary(&fs);
	close(fs.fd);
}
