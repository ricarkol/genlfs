/*
 * Copyright (c) 2018, IBM
 * Author(s): Ricardo Koller
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
 * OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Konrad E. Schroder <perseant@hhhh.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "lfs.h"
#include "config.h"

#define FSIZE ((DFL_LFSBLOCK * 130))

int test_no_space(char *log)
{
	struct fs fs;
	uint32_t avail_segs;
	off_t off;
	uint64_t nbytes;

	fs.fd = open(log, O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fs.fd != 0);

	nbytes = 1024ull;
	assert(init_lfs(&fs, nbytes) == ENOSPC);

	nbytes = 0;
	assert(init_lfs(&fs, nbytes) == ENOSPC);

	nbytes = 2 * 1024 * 1024ull;
	assert(init_lfs(&fs, nbytes) == 0);

	struct directory dir = {0};
	dir_add_entry(&dir, ".", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir, "..", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir, "bigfile", 3, LFS_DT_REG);
	dir_done(&dir);
	write_file(&fs, &dir.data[0], LFS_DIRBLKSIZ, ULFS_ROOTINO,
		LFS_IFDIR | 0755, 2, 0);

	uint64_t size = 1024 * 1024ull;
	char *largefile = malloc(size);
	assert(largefile);
	memset(largefile, '.', size);
	assert(write_file(&fs, largefile, size, 3, LFS_IFREG | 0777, 1, 0) == ENOSPC);
	close(fs.fd);
}

int test_create(char *log)
{
	struct fs fs;
	uint32_t avail_segs;
	off_t off;
	uint64_t nbytes;

	nbytes = (1024 * 1024 * 1 * 1024ull);

	fs.fd = open(log, O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fs.fd != 0);

	init_lfs(&fs, nbytes);

	struct directory dir = {0};
	dir_add_entry(&dir, ".", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir, "..", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir, "aaaaaaaaaaaaaaax", 3, LFS_DT_REG);
	dir_add_entry(&dir, "test2", 4, LFS_DT_REG);
	dir_add_entry(&dir, "test3", 5, LFS_DT_REG);
	dir_done(&dir);
	write_file(&fs, &dir.data[0], LFS_DIRBLKSIZ, ULFS_ROOTINO,
		LFS_IFDIR | 0755, 2, 0);

	char *block = malloc(FSIZE);
	assert(block);
	memset(block, '.', FSIZE);
	sprintf(&block[100], "first100bytes");
	sprintf(&block[FSIZE - 100], "last100bytes");
	write_file(&fs, block, FSIZE, 3, LFS_IFREG | 0777, 1, 0);

	struct directory dir2 = {0};
	dir_add_entry(&dir2, ".", 4, LFS_DT_DIR);
	dir_add_entry(&dir2, "..", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir2, "data2", 9, LFS_DT_REG);
	dir_done(&dir2);
	write_file(&fs, &dir2.data[0], LFS_DIRBLKSIZ, 4,
		LFS_IFDIR | 0755, 2, 0);

	sprintf(block, "/test2/data2 bla bla\n");
	write_file(&fs, block, strlen(block), 9, LFS_IFREG | 0777, 1, 0);

	struct directory dir3 = {0};
	dir_add_entry(&dir3, ".", 5, LFS_DT_DIR);
	dir_add_entry(&dir3, "..", ULFS_ROOTINO, LFS_DT_DIR);
	dir_add_entry(&dir3, "test4", 7, LFS_DT_DIR);
	dir_add_entry(&dir3, "data3", 6, LFS_DT_REG);
	dir_done(&dir3);
	write_file(&fs, &dir3.data[0], LFS_DIRBLKSIZ, 5,
		LFS_IFDIR | 0755, 2, 0);

	sprintf(block, "/test3/data3 bla bla\n");
	write_file(&fs, block, strlen(block), 6, LFS_IFREG | 0777, 1, 0);

	struct directory dir4 = {0};
	dir_add_entry(&dir4, ".", 7, LFS_DT_DIR);
	dir_add_entry(&dir4, "..", 5, LFS_DT_DIR);
	dir_add_entry(&dir4, "data4", 8, LFS_DT_REG);
	dir_done(&dir4);
	write_file(&fs, &dir4.data[0], LFS_DIRBLKSIZ, 7,
		LFS_IFDIR | 0755, 2, 0);

	sprintf(block, "/test3/test4/data4 bla bla\n");
	write_file(&fs, block, strlen(block), 8, LFS_IFREG | 0777, 1, 0);

	finish_lfs(&fs);
	close(fs.fd);

	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	test_no_space("small.lfs");

	/* XXX: should be last: some of our tests in tests.bats are using the
	 * FS created by this test. */
	test_create(argv[1]);
}
