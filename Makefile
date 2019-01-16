all: mkfs test check genlfs mkfs_small test_cksum

CFLAGS=-ggdb -O2 -Wall

run_python_tornado:
	exec ./ukvm-bin.seccomp.mmap --net=tap100 --mem=512 --disk=python_tornado.lfs python_lfs.seccomp '{"cmdline":"examples/python.bin -m bm_tornado_http","env":"PYTHONHOME=/python","net":{"if":"ukvmif0","cloner":"True","type":"inet","method":"static","addr":"10.0.0.2","mask":"16","gw":"10.0.0.1"},"blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/python"}}'

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

install: genlfs
	install -m 775 -D genlfs /usr/bin/genlfs

clean:
	rm -f mkfs test check genlfs mkfs_small
