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

#include <sys/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "lfs.h"
#include "lfs_accessors.h"
#include "config.h"

#define	SF_IMMUTABLE	0x00020000	/* file may not be changed */

#define HIGHEST_USED_INO ULFS_ROOTINO

u_int32_t lfs_sb_cksum32(struct dlfs *fs);
u_int32_t cksum(void *str, size_t len);

/* size args */
#define SIZE		(1024 * 1024 * 1024)
#define NSUPERBLOCKS	LFS_MAXNUMSB
#define MAX_INODES	(DFL_LFSBLOCK / sizeof(IFILE32))

#define NSEGS		((SIZE/DFL_LFSSEG) - 1)	/* number of segments */
#define RESVSEG		(((NSEGS/DFL_MIN_FREE_SEGS) / 2) + 1)

/*
 * calculate the maximum file size allowed with the specified block shift.
 */
#define NPTR32 ((1 << DFL_LFSBLOCK_SHIFT) / sizeof(int32_t))
#define MAXFILESIZE32 \
	((ULFS_NDADDR + NPTR32 + NPTR32 * NPTR32 + NPTR32 * NPTR32 * NPTR32) \
	<< DFL_LFSBLOCK_SHIFT)

#define NPTR64 ((1 << DFL_LFSBLOCK_SHIFT) / sizeof(int64_t))
#define MAXFILESIZE64 \
	((ULFS_NDADDR + NPTR64 + NPTR64 * NPTR64 + NPTR64 * NPTR64 * NPTR64) \
	<< DFL_LFSBLOCK_SHIFT)

#define LOG2(X) ((unsigned) (8*sizeof (unsigned long long) - __builtin_clzll((X)) - 1))

#define SECTOR_TO_BYTES(_S) (512 * (_S))
#define FSBLOCK_TO_BYTES(_S) (DFL_LFSBLOCK * (_S))
#define SEGS_TO_FSBLOCKS(_S) (((_S) * DFL_LFSSEG) / DFL_LFSBLOCK)

int main(int argc, char **argv)
{
	struct dlfs lfs;
	int fd;
	uint32_t avail_segs;
	off_t off;
	struct segment seg;
	unsigned char summary_block[lfs.dlfs_sumsize];
	char block[DFL_LFSBLOCK];
	struct lfs_dirheader32 *dir;
	struct lfs32_dinode *dinode;
	struct _cleanerinfo32 *cleanerinfo;
	struct segusage *segusage;
	int i;
	int sboffs[] = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392,
			104448, 117504};

	memset(summary_block, 0, lfs.dlfs_sumsize);

	if (argc != 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	fd = open(argv[1], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fd != 0);

	/*
	bwrite(blkno=16)

	SUPERBLOCK:

	(gdb) p *(struct dlfs *)bp->b_data
	$8 = {.dlfs_magic = 459106, dlfs_version = 2, dlfs_size = 131072,
	dlfs_ssize = 1048576, dlfs_dsize = 117878, dlfs_bsize = 8192, dlfs_fsize =
	8192, dlfs_frag = 1, dlfs_freehd = 3, dlfs_bfree = 117365, dlfs_nfiles = 0,
	dlfs_avail = -8, dlfs_uinodes = 0, dlfs_idaddr = 4, dlfs_ifile = 1,
	dlfs_lastseg = 130816, dlfs_nextseg = 128, dlfs_curseg = 0, dlfs_offset = 10,
	dlfs_lastpseg = 10, dlfs_inopf = 4, dlfs_minfree = 10, dlfs_maxfilesize =
	70403120791552, dlfs_fsbpseg = 128, dlfs_inopb = 64, dlfs_ifpb = 409, dlfs_sepb
	= 341, dlfs_nindir = 2048, dlfs_nseg = 1023, dlfs_nspf = 0, dlfs_cleansz = 1,
	dlfs_segtabsz = 3, dlfs_segmask = 0, dlfs_segshift = 0, dlfs_bshift = 13,
	dlfs_ffshift = 13, dlfs_fbshift = 0, dlfs_bmask = 8191, dlfs_ffmask = 8191,
	dlfs_fbmask = 0, dlfs_blktodb = 4, dlfs_sushift = 0, dlfs_maxsymlinklen = 60,
	dlfs_sboffs = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448,
	117504}, dlfs_nclean = 1022, dlfs_fsmnt = '\000' <repeats 89 times>,
	dlfs_pflags = 1, dlfs_dmeta = 2, dlfs_minfreeseg = 102, dlfs_sumsize = 8192,
	dlfs_serial = 1, dlfs_ibsize = 8192, dlfs_s0addr = 0, dlfs_tstamp = 0,
	dlfs_inodefmt = 0, dlfs_interleave = 0, dlfs_ident = 809671122, dlfs_fsbtodb =
	4, dlfs_resvseg = 52, dlfs_pad = '\000' <repeats 127 times>, dlfs_cksum =
	25346}
	*/

	assert(pread(fd, &lfs, sizeof(lfs),
		SECTOR_TO_BYTES(16)) == sizeof(lfs));

	assert(DFL_LFSFRAG == DFL_LFSBLOCK);
	assert(lfs.dlfs_fsbpseg > (2 + 6 + 2));
	assert(lfs.dlfs_fsbpseg < MAX_BLOCKS_PER_SEG);
	assert(lfs.dlfs_cleansz == 1);
	assert(lfs.dlfs_segtabsz == 3);
	assert(lfs.dlfs_nseg == 1023);
	assert(lfs.dlfs_size == 131072);
	assert(lfs.dlfs_dsize == 117878);
	assert(lfs.dlfs_lastseg == 130816);
	assert(lfs.dlfs_inopf == 4);
	assert(lfs.dlfs_maxfilesize == 70403120791552);
	assert(lfs.dlfs_fsbpseg == 128);
	assert(lfs.dlfs_minfreeseg == 102);
	assert(lfs.dlfs_fsbtodb == 4);
	assert(lfs.dlfs_resvseg == 52);
	assert(lfs.dlfs_cleansz == 1);
	assert(sizeof(CLEANERINFO32) <= DFL_LFSBLOCK);
	assert(sizeof(CLEANERINFO64) <= DFL_LFSBLOCK);
	assert(lfs.dlfs_segtabsz == 3);
	assert(lfs.dlfs_blktodb == 4);
	assert(lfs.dlfs_sumsize == 8192);
	assert(lfs.dlfs_pflags == 1);
	assert(lfs.dlfs_pflags == LFS_PF_CLEAN);
	assert(lfs.dlfs_nclean = 1022);
	assert(lfs.dlfs_curseg == 0);
	assert(lfs.dlfs_nextseg == 128);
	assert(lfs.dlfs_idaddr == 5);

	for (i = 1; i < 10; i++)
		assert(lfs.dlfs_sboffs[i] == sboffs[i]);

	/*
	bwrite(blkno=32)

	SEGMENT SUMMARY:

	(gdb) p *(struct segsum32 *)bp->b_data
	$31 = {ss_sumsum = 28386, ss_datasum = 33555, ss_magic = 398689, ss_next = 128,
	ss_ident = 249755386, ss_nfinfo = 2, ss_ninos = 2, ss_f lags = 8, ss_pad =
	"\000", ss_reclino = 0, ss_serial = 1, ss_create = 0}

	FINFO 1:

	(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32))
	$5 = {fi_nblocks = 1, fi_version = 1, fi_ino = 2, fi_lastlength = 8192} (gdb) p
	*(int *)(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32)) $28 = 0

	FINFO 2:

	(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32) +
	sizeof(struct finfo32) + sizeof(int))
	$30 = {fi_nblocks = 5, fi_version = 1, fi_ino = 1, fi_lastlength = 8192} (gdb)
	p *(int (*)[5])(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32) +
	sizeof(int) + sizeof(struct finfo32)) $31 = {0, 1, 2, 3, 4}
	*/

	assert(pread(fd, summary_block, lfs.dlfs_sumsize,
		SECTOR_TO_BYTES(32)) == lfs.dlfs_sumsize);

	assert(((struct segsum32 *)summary_block)->ss_magic == 398689);
	assert(((struct segsum32 *)summary_block)->ss_next == 128);
	assert(((struct segsum32 *)summary_block)->ss_nfinfo == 2);
	assert(((struct segsum32 *)summary_block)->ss_ninos == 2);
	assert(((struct segsum32 *)summary_block)->ss_serial == 1);
	struct finfo32 *finfo1 = (struct finfo32 *)(summary_block +
		sizeof(struct segsum32));
	assert(finfo1->fi_nblocks == 1);
	assert(finfo1->fi_version == 1);
	assert(finfo1->fi_ino == 2);
	assert(finfo1->fi_lastlength = 8192);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + 0*sizeof(int)) == 0);

	struct finfo32 *finfo2 = (struct finfo32 *)(summary_block +
		sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int));
	assert(finfo2->fi_nblocks == 5);
	assert(finfo2->fi_version == 1);
	assert(finfo2->fi_ino == 1);
	assert(finfo2->fi_lastlength = 8192);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 0*sizeof(int)) == 0);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 1*sizeof(int)) == 1);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 2*sizeof(int)) == 2);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 3*sizeof(int)) == 3);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 4*sizeof(int)) == 4);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 5*sizeof(int)) == 0);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 6*sizeof(int)) == 0);
	size_t end = sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + 5*sizeof(int);

	/*
	bwrite(blkno=48)

	Data for root directory:

	(gdb) p *(struct lfs_dirheader32 *)(bp->b_data)
	$53 = {dh_ino = 2, dh_reclen = 12, dh_type = 4 '\004', dh_namlen = 1
	'\001'} (gdb) printf "%s\n", (char *)(bp->b_data + sizeof(struct
	lfs_dirheader32))
	.
	(gdb) p *(struct lfs_dirheader32 *)(bp->b_data + sizeof(struct
	lfs_dirheader32) + 4)
	$55 = {dh_ino = 2, dh_reclen = 500, dh_type = 4 '\004', dh_namlen = 2
	'\002'} (gdb) printf "%s\n", (char *)(bp->b_data + sizeof(struct
	lfs_dirheader32) + 4 + sizeof(struct lfs_dirheader32))
	..
	*/
	assert(pread(fd, block, sizeof(block),
		SECTOR_TO_BYTES(48)) == sizeof(block));
	/* . */
	dir = (struct lfs_dirheader32 *)(block);
	assert(dir->dh_ino == 2);
	assert(dir->dh_reclen == 12);
	assert(dir->dh_type == 4);
	assert(dir->dh_namlen == 1);
	assert(memcmp((char *)dir + sizeof(struct lfs_dirheader32), ".", 1) == 0);
	/* .. */
	dir = (struct lfs_dirheader32 *)(block + sizeof(struct lfs_dirheader32) + 4);
	assert(dir->dh_ino == 2);
	assert(dir->dh_reclen == 500);
	assert(dir->dh_type == 4);
	assert(dir->dh_namlen == 2);
	assert(memcmp((char *)dir + sizeof(struct lfs_dirheader32), "..", 2) == 0);

	/*
	bwrite(blkno=64)

	INODE for root directory:

	(gdb) p *(struct lfs32_dinode *)bp->b_data
	$60 = {di_mode = 16877, di_nlink = 2, di_inumber = 2, di_size = 512,
	di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec =
	0, di_ctime = 1534531037, di_ctimensec = 0, di_db = {3, 0 <repeats 11 times>},
	di_ib = {0, 0, 0}, di_flags = 0, di_blocks = 1, di_gen = 1, di_uid = 0, di_gid
	= 0, di_modrev = 0}
	*/
	assert(pread(fd, block, sizeof(block),
		SECTOR_TO_BYTES(64)) == sizeof(block));

	dinode = (struct lfs32_dinode *)block;
	assert(dinode->di_mode == 16877);
	assert(dinode->di_nlink == 2);
	assert(dinode->di_inumber == ULFS_ROOTINO);
	assert(dinode->di_size == 512);
	assert(dinode->di_atime > 1534531037);
	assert(dinode->di_atimensec == 0);
	assert(dinode->di_mtime > 1534531037);
	assert(dinode->di_mtimensec == 0);
	assert(dinode->di_ctime > 1534531037);
	assert(dinode->di_ctimensec == 0);
	assert(dinode->di_db[0] == 3);
	for (i = 1; i < 12; i++)
		assert(dinode->di_db[i] == 0);
	for (i = 0; i < 3; i++)
		assert(dinode->di_ib[i] == 0);
	assert(dinode->di_flags == 0);
	assert(dinode->di_blocks == 1);
	assert(dinode->di_gen == 1);
	assert(dinode->di_uid == 0);
	assert(dinode->di_gid == 0);
	assert(dinode->di_modrev == 0);

	/*
	bwrite(blkno=80)

	INODE for ifile:

	(gdb) p *(struct lfs32_dinode *)(bp->b_data + sizeof(struct lfs32_dinode))
	$61 = {di_mode = 33152, di_nlink = 1, di_inumber = 1, di_size = 40960,
	di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec =
	0, di_ctime = 153453103 7, di_ctimensec = 0, di_db = {6, 7, 8, 9, 10, 0, 0, 0,
	0, 0, 0, 0}, di_ib = {0, 0, 0}, di_flags = 131072, di_blocks = 5, di_gen = 1,
	di_uid = 0, di_gid = 0, di_modrev = 0}
	*/
	assert(pread(fd, block, sizeof(block),
		SECTOR_TO_BYTES(80)) == sizeof(block));

	dinode = (struct lfs32_dinode *)block;
	assert(dinode->di_mode == 33152);
	assert(dinode->di_nlink == 1);
	assert(dinode->di_inumber == 1);
	assert(dinode->di_size == 40960);
	assert(dinode->di_atime > 1534531037);
	assert(dinode->di_atimensec == 0);
	assert(dinode->di_mtime > 1534531037);
	assert(dinode->di_mtimensec == 0);
	assert(dinode->di_ctime > 1534531037);
	assert(dinode->di_ctimensec == 0);
	assert(dinode->di_db[0] == 6);
	assert(dinode->di_db[1] == 7);
	assert(dinode->di_db[2] == 8);
	assert(dinode->di_db[3] == 9);
	assert(dinode->di_db[4] == 10);
	for (i = 5; i < 12; i++)
		assert(dinode->di_db[i] == 0);
	for (i = 0; i < 3; i++)
		assert(dinode->di_ib[i] == 0);
	assert(dinode->di_flags == 131072);
	assert(dinode->di_blocks == 5);
	assert(dinode->di_gen == 1);
	assert(dinode->di_uid == 0);
	assert(dinode->di_gid == 0);
	assert(dinode->di_modrev == 0);

	/*
	bwrite(blkno=96)

	IFILE/CLEANER INFO:

	(gdb) p *(struct _cleanerinfo32 *)(bp->b_data)
	$134 = {clean = 1022, dirty = 1, bfree = 117367, avail = -6, free_head
	= 3, free_tail = 408, flags = 0}
	*/
	assert(pread(fd, block, sizeof(block),
		SECTOR_TO_BYTES(96)) == sizeof(block));

	cleanerinfo = (struct _cleanerinfo32 *)block;
	assert(cleanerinfo->clean == 1022);
	assert(cleanerinfo->dirty == 1);
	assert(cleanerinfo->free_head == 3);
	assert(cleanerinfo->free_tail == 408);
	assert(cleanerinfo->flags == 0);

	/*
	bwrite(blkno=112)

	IFILE/SEGUSAGE (block 1):

	(gdb) p *(struct segusage *)(bp->b_data)
	$135 = {su_nbytes = 40960, su_olastmod = 0, su_nsums = 1, su_ninos = 1,
	su_flags = 7, su_lastmod = 0}

	bwrite(blkno=128)

	IFILE/SEGUSAGE (block 2):

	(gdb) p *(struct segusage *)(bp->b_data)
	$135 = {su_nbytes = 0, su_olastmod = 0, su_nsums = 0, su_ninos = 0,
	su_flags = 10, su_lastmod = 0}

	bwrite(blkno=144)

	IFILE/SEGUSAGE (block 3):

	(gdb) p *(struct segusage *)(bp->b_data)
	$135 = {su_nbytes = 0, su_olastmod = 0, su_nsums = 0, su_ninos = 0,
	su_flags = 10, su_lastmod = 0}
	*/

	struct segusage segusages[NSEGS];
	assert(pread(fd, segusages, sizeof(segusages),
		SECTOR_TO_BYTES(112)) == sizeof(segusages));

	assert(segusages[0].su_nsums == 1);
	assert(segusages[0].su_ninos == 2);
	assert(segusages[0].su_nbytes == 40960);
	assert(segusages[0].su_flags == SEGUSE_ACTIVE|SEGUSE_DIRTY|SEGUSE_SUPERBLOCK);
	assert(segusages[0].su_lastmod == 0);
	for (i = 1; i < NSEGS; i++) {
		int found = 0, j;
		for (j = 1; j < 10; j++) {
			if (i == (sboffs[j]*8192)/(1024*1024))
				found = 1;
		}
		if (found) {
			assert(segusages[i].su_flags == SEGUSE_SUPERBLOCK);
		} else {
			assert(segusages[i].su_flags == SEGUSE_EMPTY);
		}
		assert(segusages[i].su_nsums == 0);
		assert(segusages[i].su_ninos == 0);
		assert(segusages[i].su_nbytes == 0);
		assert(segusages[i].su_lastmod == 0);
	}

	/*
	bwrite(blkno=160)

	IFILE/INODE MAP:

	(gdb) p *(IFILE32 (*)[10])(bp->b_data)
	$155 = {
	{if_version = 0, if_daddr = 0, if_nextfree = 0, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 0, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 4, if_nextfree = 0, if_atime_sec = 0, if_atime_nsec = 0}, <== INODE 2 at BLOCK=4 (lbn=64)
	{if_version = 1, if_daddr = 0, if_nextfree = 4, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 5, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 6, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 7, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 8, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 9, if_atime_sec = 0, if_atime_nsec = 0},
	{if_version = 1, if_daddr = 0, if_nextfree = 10, if_atime_sec = 0, if_atime_nsec = 0}}
	*/
	IFILE32 ifiles[MAX_INODES];
	assert(sizeof(ifiles) < 8192);
	assert(pread(fd, ifiles, sizeof(ifiles),
		SECTOR_TO_BYTES(160)) == sizeof(ifiles));

	assert(ifiles[0].if_version == 0);
	assert(ifiles[0].if_daddr == 0);
	assert(ifiles[0].if_nextfree == 0);
	assert(ifiles[0].if_atime_sec == 0);
	assert(ifiles[0].if_atime_nsec == 0);

	assert(ifiles[LFS_IFILE_INUM].if_version == 1);
	assert(ifiles[LFS_IFILE_INUM].if_daddr == 5);
	assert(ifiles[LFS_IFILE_INUM].if_nextfree == 0);
	assert(ifiles[LFS_IFILE_INUM].if_atime_sec == 0);
	assert(ifiles[LFS_IFILE_INUM].if_atime_nsec == 0);

	assert(ifiles[ULFS_ROOTINO].if_version == 1);
	assert(ifiles[ULFS_ROOTINO].if_daddr == 4);
	assert(ifiles[ULFS_ROOTINO].if_nextfree == 0);
	assert(ifiles[ULFS_ROOTINO].if_atime_sec == 0);
	assert(ifiles[ULFS_ROOTINO].if_atime_nsec == 0);

	for (i = 3; i < MAX_INODES; i++) {
		assert(ifiles[i].if_version == 1);
		assert(ifiles[i].if_daddr == LFS_UNUSED_DADDR);
		assert(ifiles[i].if_nextfree == i + 1);
		assert(ifiles[i].if_atime_sec == 0);
		assert(ifiles[i].if_atime_nsec == 0);
	}

	return 0;
}
