#!/usr/bin/env bats

@test "check against a netbsd lfs formatted disk" {
	run rm -f test.lfs
	run ./mkfs test.lfs
	echo "$output"
	[ "$status" -eq 0 ]
	run ./check test.lfs
	echo "$output"
	[ "$status" -eq 0 ]
}

@test "check against a netbsd lfs formatted disk (fail)" {
	run rm -f test.lfs
	run ./mkfs test.lfs
	# this should make the ./check fail
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	run ./check test.lfs
	echo "$output"
	[ "$status" -eq 134 ]
}

@test "check /" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"aaaaaaaaaaaaaaax"* ]]
	[[ "$output" == *"test2"* ]]
	[[ "$output" == *"test3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "large file" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/aaaaaaaaaaaaaaax","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"........................"* ]]
	[[ "$output" == *"first100bytes"* ]]
	[[ "$output" == *"last100bytes"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test2/" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test2/data2" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"test4"* ]]
	[[ "$output" == *"data3"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/data3" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/data3","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/data3 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/test4/" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data4"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/test4/data4" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/test4/../test4/" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/../test4/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data4"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/test4/../test4/data4" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/../test4/data4","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test3/test4/data4 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}


@test "check test3/test4/../../test2/" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/../../test2/","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"."* ]]
	[[ "$output" == *".."* ]]
	[[ "$output" == *"data2"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}

@test "check test3/test4/../../test2/data2" {
	run rm -f test.lfs
	run ./test test.lfs
	run ./ukvm-bin.seccomp --disk=test.lfs blk-rumprun.seccomp '{"cmdline":"blk /test/test3/test4/../../test2/data2","blk":{"source":"etfs","path":"/dev/ld0a","fstype":"blk","mountpoint":"/test"}}'
	echo "$output"
	[[ "$output" == *"test2/data2 bla bla"* ]]
	[[ "$output" == *"=== main() of \"blk\" returned 0 ==="* ]]
}
