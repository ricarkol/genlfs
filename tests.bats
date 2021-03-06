#!/usr/bin/env bats

function setup() {
	rm -f test.lfs
}

function create_tree() {
	rm -rf test_dir
	mkdir -p test_dir
	dd if=/dev/zero of=test_dir/huge bs=1M count=1k
	echo first100bytes > test_dir/aaaaaaaaaaaaaaax
	yes "........................" | dd of=test_dir/aaaaaaaaaaaaaaax conv=notrunc seek=1 bs=2k count=1024
	echo last100bytes >> test_dir/aaaaaaaaaaaaaaax
	mkdir -p test_dir/test2
	echo "test2/data2 bla bla" > test_dir/test2/data2
	mkdir -p test_dir/test3
	echo "test3/data3 bla bla" > test_dir/test3/data3
	mkdir -p test_dir/test3/test4
	echo "test3/test4/data4 bla bla" > test_dir/test3/test4/data4
	for i in `seq 1 100`; do echo $i > test_dir/blablabasdfasdfasdfasdfasdfasdfasdfasdfasdfasdf$i; done
}

@test "mkfs: check against a netbsd lfs formatted disk" {
	run ./mkfs_small test.lfs
	echo "$output"
	[ "$status" -eq 0 ]
	run ./check test.lfs
	echo "$output"
	[ "$status" -eq 0 ]
}

@test "mkfs: check against a netbsd lfs formatted disk (fail)" {
	run ./mkfs_small test.lfs
	# this should make the ./check fail
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	run ./check test.lfs
	echo "$output"
	[ "$status" -eq 134 ]
}

@test "test: check /" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"aaaaaaaaaaaaaaax"* ]]
	[[ "$output" == *"test2"* ]]
	[[ "$output" == *"test3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: large file" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/aaaaaaaaaaaaaaax","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"........................"* ]]
	[[ "$output" == *"first100bytes"* ]]
	#[[ "$output" == *"last100bytes"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test2/" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test2/data2" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"test4"* ]]
	[[ "$output" == *"data3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/data3" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/data3","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/data3 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/test4/" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data4"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/test4/data4" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/test4/../test4/" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../test4/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data4"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/test4/../test4/data4" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}


@test "test: check test3/test4/../../test2/" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../../test2/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "test: check test3/test4/../../test2/data2" {
	run ./test test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../../test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "genlfs: large file" {
	#skip "Takes too much time"

	create_tree
	rm -rf test_dir
	mkdir -p test_dir
	seq 1 10000000 > test_dir/large

	run ./genlfs test_dir test.lfs
	echo "$output"
	[ "$status" -eq 0 ]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/large","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"10000000"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "genlfs: empty file" {
	create_tree
	rm -rf test_dir
	mkdir -p test_dir
	touch test_dir/empty

	run ./genlfs test_dir test.lfs
	echo "$output"
	[ "$status" -eq 0 ]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"empty"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "genlfs: check cksum" {
	export cksum=`./test_cksum blk-rumprun.spt`
	echo "cksum: $cksum"
	mkdir -p test_cksum_dir
	cp blk-rumprun.spt test_cksum_dir/.
	run ./genlfs test_cksum_dir test.lfs
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/blk-rumprun.spt","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"cksum: $cksum"* ]]
}

@test "genlfs: check tree" {
	create_tree
	run ./genlfs test_dir test.lfs
	echo "$output"
	[ "$status" -eq 0 ]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"aaaaaaaaaaaaaaax"* ]]
	[[ "$output" == *"huge"* ]]
	[[ "$output" == *"test2"* ]]
	[[ "$output" == *"test3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	export cksum=`./test_cksum test_dir/aaaaaaaaaaaaaaax`
	echo "cksum: $cksum"
	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/aaaaaaaaaaaaaaax","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"cksum: $cksum"* ]]
	[[ "$output" == *"........................"* ]]
	[[ "$output" == *"first100bytes"* ]]
	#[[ "$output" == *"last100bytes"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"test4"* ]]
	[[ "$output" == *"data3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../test4/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data4"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../../test2/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]

	run ./solo5-spt --disk=test.lfs blk-rumprun.spt '{"cmdline":"blk /test/test3/test4/../../test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}
