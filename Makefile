all: mkfs test check genlfs

mkfs: mkfs.c lfs.c lfs_cksum.c
	gcc -ggdb mkfs.c lfs.c lfs_cksum.c -o mkfs

test: test.c lfs.c lfs_cksum.c
	gcc -ggdb test.c lfs.c lfs_cksum.c -o test

check: check.c lfs_cksum.c
	gcc -ggdb check.c lfs_cksum.c -o check

genlfs: genlfs.c lfs.c lfs_cksum.c
	gcc -ggdb -o $@ genlfs.c lfs.c lfs_cksum.c

tests:
	bats tests.bats

clean:
	rm -f mkfs test check genlfs
