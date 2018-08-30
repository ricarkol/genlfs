all:
	gcc -ggdb mkfs.c lfs_cksum.c -o mkfs
	gcc -ggdb check.c lfs_cksum.c -o check
