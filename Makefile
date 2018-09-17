all:
	gcc -ggdb mkfs.c lfs.c lfs_cksum.c -o mkfs
	gcc -ggdb test.c lfs.c lfs_cksum.c -o test
	gcc -ggdb check.c lfs_cksum.c -o check
