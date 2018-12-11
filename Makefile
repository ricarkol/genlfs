all: mkfs test check genlfs mkfs_small test_cksum

CFLAGS=-ggdb -O2 -Wall

# mkfs_small creates a small LFS disk as created by the netbsd newfs_lfs tool
mkfs_small: mkfs.c lfs.c lfs_cksum.c
	gcc -DDIRSIZE=8192 -DIFILE_MAP_SZ=1 ${CFLAGS} mkfs.c lfs.c lfs_cksum.c -o $@

check: check.c lfs_cksum.c
	gcc -DIFILE_MAP_SZ=1 -DDIRSIZE=8192 ${CFLAGS} check.c lfs_cksum.c -o $@

mkfs: mkfs.c lfs.c lfs_cksum.c
	gcc ${CFLAGS} mkfs.c lfs.c lfs_cksum.c -o $@

test: test.c lfs.c lfs_cksum.c
	gcc ${CFLAGS} test.c lfs.c lfs_cksum.c -o $@

genlfs: genlfs.c lfs.c lfs_cksum.c
	gcc ${CFLAGS} -o $@ genlfs.c lfs.c lfs_cksum.c

test_cksum: test_cksum.c
	gcc ${CFLAGS} test_cksum.c -o test_cksum

tests: all
	bats tests.bats

clean:
	rm -f mkfs test check genlfs mkfs_small
