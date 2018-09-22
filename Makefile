all: mkfs test check genlfs

mkfs:
	gcc -ggdb mkfs.c lfs.c lfs_cksum.c -o mkfs

test:
	gcc -ggdb test.c lfs.c lfs_cksum.c -o test

check:
	gcc -ggdb check.c lfs_cksum.c -o check

genlfs: genlfs.c lfs.c lfs_cksum.c
	gcc -ggdb -o $@ genlfs.c lfs.c lfs_cksum.c

tests:
	bats tests.bats
