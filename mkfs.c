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

#include "lfs.h"
#include "lfs_accessors.h"
#include "config.h"

#define	SF_IMMUTABLE	0x00020000	/* file may not be changed */

#define HIGHEST_USED_INO ULFS_ROOTINO

u_int32_t lfs_sb_cksum32(struct dlfs *fs);

/* size args */
#define SIZE		(1024 * 1024 * 1024)
#define NSUPERBLOCKS	10

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

static const struct dlfs dlfs32_default = {
	.dlfs_magic =		LFS_MAGIC,
	.dlfs_version =		LFS_VERSION,
	.dlfs_size =		SIZE/DFL_LFSBLOCK,
	.dlfs_ssize =		DFL_LFSSEG,
	.dlfs_dsize =	((NSEGS - NSEGS/DFL_MIN_FREE_SEGS) * DFL_LFSSEG - 
			DFL_LFSBLOCK * NSUPERBLOCKS) / DFL_LFSBLOCK,
	.dlfs_bsize =		DFL_LFSBLOCK,
	.dlfs_fsize =		DFL_LFSFRAG,
	.dlfs_frag =		DFL_LFSBLOCK/DFL_LFSFRAG,
	.dlfs_freehd =		HIGHEST_USED_INO + 1,
	.dlfs_uinodes =		0,
	.dlfs_idaddr =		0,
	.dlfs_ifile =		LFS_IFILE_INUM,
	.dlfs_lastseg =		(SIZE - 2*DFL_LFSSEG) / DFL_LFSBLOCK,

	/*
	 * Initial state:
	 * We start at block 2, as block 0 is just padding and block 1
	 * is the superblock.
	 */
	.dlfs_offset =		2,
	.dlfs_lastpseg =	2,
	.dlfs_nextseg =		DFL_LFSSEG/DFL_LFSBLOCK,
	.dlfs_curseg =		0,
	.dlfs_bfree =	((NSEGS - NSEGS/DFL_MIN_FREE_SEGS) * DFL_LFSSEG - 
			DFL_LFSBLOCK * NSUPERBLOCKS) / DFL_LFSBLOCK,
	.dlfs_nfiles =		0,
	/* 1 for the start padding (block 0)  */
	.dlfs_avail =		SEGS_TO_FSBLOCKS((SIZE/DFL_LFSSEG) - RESVSEG) -
				NSUPERBLOCKS - 1,

	.dlfs_inopf =		512/sizeof(struct lfs32_dinode),
	.dlfs_minfree =		MINFREE,
	.dlfs_maxfilesize =	MAXFILESIZE32,
	.dlfs_fsbpseg =		DFL_LFSSEG/DFL_LFSFRAG,
	.dlfs_inopb =		DFL_LFSBLOCK/sizeof(struct lfs32_dinode),
	.dlfs_ifpb =		DFL_LFSBLOCK/sizeof(IFILE32),
	.dlfs_sepb =		DFL_LFSBLOCK/sizeof(SEGUSE),
	.dlfs_nindir =		DFL_LFSBLOCK/sizeof(int32_t),
	.dlfs_nseg =		NSEGS,
	.dlfs_nspf =		0,
	.dlfs_cleansz =		1,
	.dlfs_segtabsz =	(NSEGS + DFL_LFSBLOCK/sizeof(SEGUSE) - 1) /
				(DFL_LFSBLOCK/sizeof(SEGUSE)),
	.dlfs_segmask =		DFL_LFSSEG_MASK,
	.dlfs_segshift =	DFL_LFSSEG_SHIFT,
	.dlfs_bshift =		DFL_LFSBLOCK_SHIFT,
	.dlfs_ffshift =		DFL_LFS_FFSHIFT,
	.dlfs_fbshift =		DFL_LFS_FBSHIFT,
	.dlfs_bmask =		DFL_LFSBLOCK_MASK,
	.dlfs_ffmask =		DFL_LFS_FFMASK,
	.dlfs_fbmask =		DFL_LFS_FBMASK,
	.dlfs_blktodb =		LOG2(DFL_LFSBLOCK/512),
	.dlfs_sushift =		0,
	.dlfs_maxsymlinklen =	LFS32_MAXSYMLINKLEN,
	.dlfs_sboffs =		{ 0 },
	.dlfs_sboffs = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448, 117504},
	.dlfs_nclean =  	NSEGS - 1,
	.dlfs_fsmnt =   	{ 0 },
	.dlfs_pflags =  	LFS_PF_CLEAN,
	.dlfs_dmeta =		0,
	.dlfs_minfreeseg =	(NSEGS/DFL_MIN_FREE_SEGS),
	.dlfs_sumsize =		DFL_LFSFRAG,
	.dlfs_serial =		0,
	.dlfs_ibsize =		DFL_LFSFRAG,
	.dlfs_s0addr =		0,
	.dlfs_tstamp =  	0,
	.dlfs_inodefmt =	LFS_44INODEFMT,
	.dlfs_interleave =	0,
	.dlfs_ident =		0,
	.dlfs_fsbtodb =		LOG2(DFL_LFSBLOCK/512),
	.dlfs_resvseg =		RESVSEG,

	.dlfs_pad = 		{ 0 },
	.dlfs_cksum =		0
};

static const struct dlfs64 dlfs64_default = {
	.dlfs_magic =		LFS64_MAGIC,
	.dlfs_version =		LFS_VERSION,
	.dlfs_size =		0,
	.dlfs_dsize =	((NSEGS - NSEGS/DFL_MIN_FREE_SEGS) * DFL_LFSSEG -
			DFL_LFSBLOCK*10) / DFL_LFSBLOCK,
	.dlfs_ssize =		DFL_LFSSEG,
	.dlfs_bsize =		DFL_LFSBLOCK,
	.dlfs_fsize =		DFL_LFSFRAG,
	.dlfs_frag =		DFL_LFSBLOCK/DFL_LFSFRAG,
	.dlfs_freehd =		HIGHEST_USED_INO + 1,
	.dlfs_idaddr =		0,
	.dlfs_uinodes =		0,
	.dlfs_unused_0 =	0,
	.dlfs_lastseg =		(SIZE - 2*DFL_LFSSEG) / DFL_LFSBLOCK,

	/*
	 * Initial state:
	 * We start at block 2, as block 0 is just padding and block 1
	 * is the superblock.
	 */
	.dlfs_offset =		2,
	.dlfs_lastpseg =	2,
	.dlfs_nextseg =		DFL_LFSSEG/DFL_LFSBLOCK,
	.dlfs_curseg =		0,
	.dlfs_bfree =		0, /* ??? */
	.dlfs_nfiles =		0,
	/* 1 for the start padding (block 0)  */
	.dlfs_avail =		SEGS_TO_FSBLOCKS((SIZE/DFL_LFSSEG) - RESVSEG) -
				NSUPERBLOCKS - 1,

	.dlfs_inopf =		512/sizeof(struct lfs64_dinode),
	.dlfs_minfree =		MINFREE,
	.dlfs_maxfilesize =	MAXFILESIZE64,
	.dlfs_fsbpseg =		DFL_LFSSEG/DFL_LFSFRAG,
	.dlfs_inopb =		DFL_LFSBLOCK/sizeof(struct lfs64_dinode),
	.dlfs_ifpb =		DFL_LFSBLOCK/sizeof(IFILE64),
	.dlfs_sepb =		DFL_LFSBLOCK/sizeof(SEGUSE),
	.dlfs_nindir =		DFL_LFSBLOCK/sizeof(int64_t),
	.dlfs_nseg =		NSEGS,
	.dlfs_nspf =		0,
	.dlfs_cleansz =		1,
	.dlfs_segtabsz =	(NSEGS + DFL_LFSBLOCK/sizeof(SEGUSE) - 1) /
				(DFL_LFSBLOCK/sizeof(SEGUSE)),
	.dlfs_bshift =		DFL_LFSBLOCK_SHIFT,
	.dlfs_ffshift =		DFL_LFS_FFSHIFT,
	.dlfs_fbshift =		DFL_LFS_FBSHIFT,
	.dlfs_bmask =		DFL_LFSBLOCK_MASK,
	.dlfs_ffmask =		DFL_LFS_FFMASK,
	.dlfs_fbmask =		DFL_LFS_FBMASK,
	.dlfs_blktodb =		LOG2(DFL_LFSBLOCK/512),
	.dlfs_sushift =		0,
	.dlfs_sboffs =		{ 0 },
	.dlfs_maxsymlinklen =	LFS64_MAXSYMLINKLEN,
	.dlfs_nclean =  	NSEGS - 1,
	.dlfs_fsmnt =   	{ 0 },
	.dlfs_pflags =  	LFS_PF_CLEAN,
	.dlfs_dmeta =		0,
	.dlfs_minfreeseg =	(NSEGS/DFL_MIN_FREE_SEGS),
	.dlfs_sumsize =		DFL_LFSFRAG,
	.dlfs_ibsize =		DFL_LFSFRAG,
	.dlfs_inodefmt =	LFS_44INODEFMT,
	.dlfs_serial =		0,
	.dlfs_s0addr =		0,
	.dlfs_tstamp =  	0,
	.dlfs_interleave =	0,
	.dlfs_ident =		0,
	.dlfs_fsbtodb =		LOG2(DFL_LFSBLOCK/512),
	.dlfs_resvseg =		RESVSEG,

	.dlfs_pad = 		{ 0 },
	.dlfs_cksum =		0
};


int write_superblock(int fd, struct dlfs *lfs)
{
	int i;

	lfs->dlfs_serial++;
	lfs->dlfs_cksum = lfs_sb_cksum32(lfs);

	for (i = 0; i < NSUPERBLOCKS; i++)
		assert(pwrite(fd, lfs, sizeof(*lfs),
			FSBLOCK_TO_BYTES(lfs->dlfs_sboffs[i])) == sizeof(*lfs));
	return 0;
}

void advance_log(struct dlfs *lfs)
{
	lfs->dlfs_offset++;
	lfs->dlfs_lastpseg++;
	lfs->dlfs_avail--;
	lfs->dlfs_bfree--;
}

int main(int argc, char **argv)
{
	struct dlfs lfs = dlfs32_default;
	int fd;
	uint32_t avail_segs;
	off_t off;

	/* XXX: This makes things a lot simpler. */
	assert(DFL_LFSFRAG == DFL_LFSBLOCK);

	if (argc != 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	fd = open(argv[1], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fd != 0);
/*
bwrite(blkno=32)

SEGMENT SUMMARY:

(gdb) p *(struct segsum32 *)bp->b_data
$31 = {ss_sumsum = 28386, ss_datasum = 33555, ss_magic = 398689, ss_next = 128, ss_ident = 249755386, ss_nfinfo = 2, ss_ninos = 2, ss_f
lags = 8, ss_pad = "\000", ss_reclino = 0, ss_serial = 1, ss_create = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	struct segsum32 segsum = {
		.ss_sumsum = 28386,
		.ss_datasum = 33555,
		.ss_magic = 398689,
		.ss_next = 128,
		.ss_ident = 249755386,
		.ss_nfinfo = 2,
		.ss_ninos = 2,
		.ss_flags = 8,
		.ss_pad = "\000",
		.ss_reclino = 0,
		.ss_serial = 1,
		.ss_create = 0
	};
	assert(pwrite(fd, &lfs, sizeof(segsum), off) == sizeof(segsum));
	off += sizeof(segsum);

	int ninos = (segsum.ss_ninos + lfs.dlfs_inopb - 1) / lfs.dlfs_inopb;
	lfs.dlfs_dmeta += (lfs.dlfs_sumsize + ninos * lfs.dlfs_ibsize) / DFL_LFSBLOCK;
	}
/*
FINFO 1:

(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32))
$5 = {fi_nblocks = 1, fi_version = 1, fi_ino = 2, fi_lastlength = 8192}
(gdb) p *(int *)(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32))
$28 = 0
*/
	{
	int i;
	struct finfo32 finfo = {
		.fi_nblocks = 1,
		.fi_version = 1,
		.fi_ino = 2,
		.fi_lastlength = 8192
	};
	assert(pwrite(fd, &finfo, sizeof(finfo), off) == sizeof(finfo));
	off += sizeof(finfo);
	for (i = 0; i < finfo.fi_nblocks; i++) {
		IINFO32 iinfo = { .ii_block = i };
		assert(pwrite(fd, &iinfo, sizeof(iinfo), off) == sizeof(iinfo));
		off += sizeof(iinfo);
	}
	}

/*
FINFO 2:

(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int))
$30 = {fi_nblocks = 5, fi_version = 1, fi_ino = 1, fi_lastlength = 8192}
(gdb) p *(int (*)[5])(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int) + sizeof(struct finfo32))
$31 = {0, 1, 2, 3, 4}
*/

	{
	int i;
	struct finfo32 finfo = {
		.fi_nblocks = 5,
		.fi_version = 1,
		.fi_ino = 1,
		.fi_lastlength = 8192
	};
	assert(pwrite(fd, &finfo, sizeof(finfo), off) == sizeof(finfo));
	off += sizeof(finfo);
	for (i = 0; i < finfo.fi_nblocks; i++) {
		IINFO32 iinfo = { .ii_block = i };
		assert(pwrite(fd, &iinfo, sizeof(iinfo), off) == sizeof(iinfo));
		off += sizeof(iinfo);
	}
	}

	advance_log(&lfs);
/*
Data for root directory:

bwrite(blkno=48)

(gdb) p *(struct lfs_dirheader32 *)(bp->b_data)
$53 = {dh_ino = 2, dh_reclen = 12, dh_type = 4 '\004', dh_namlen = 1 '\001'}
(gdb) printf "%s\n", (char *)(bp->b_data + sizeof(struct lfs_dirheader32))
.
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(48));
	struct lfs_dirheader32 dir = {
		.dh_ino = 2,
		.dh_reclen = 12,
		.dh_type = 4,
		.dh_namlen = 1
	};
	assert(pwrite(fd, &dir, sizeof(dir), off) == sizeof(dir));
	off += sizeof(dir);
	char buf[] = ".";
	assert(pwrite(fd, &buf, 1, off) == 1);
	off += 4;
	}

/*
(gdb) p *(struct lfs_dirheader32 *)(bp->b_data + sizeof(struct lfs_dirheader32) + 4)
$55 = {dh_ino = 2, dh_reclen = 500, dh_type = 4 '\004', dh_namlen = 2 '\002'}
(gdb) printf "%s\n", (char *)(bp->b_data + sizeof(struct lfs_dirheader32) + 4 + sizeof(struct lfs_dirheader32))
..
*/
	{
	struct lfs_dirheader32 dir = {
		.dh_ino = 2,
		.dh_reclen = 500,
		.dh_type = 4,
		.dh_namlen = 2
	};
	assert(pwrite(fd, &dir, sizeof(dir), off) == sizeof(dir));
	off += sizeof(dir);
	char buf[] = "..";
	assert(pwrite(fd, &buf, 2, off) == 2);
	off += 4;
	}

	advance_log(&lfs);
/*
bwrite(blkno=64)

INODE for root directory:

(gdb) p *(struct lfs32_dinode *)bp->b_data
$60 = {di_mode = 16877, di_nlink = 2, di_inumber = 2, di_size = 512, di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec = 0, di_ctime = 1534531037,
 di_ctimensec = 0, di_db = {3, 0 <repeats 11 times>}, di_ib = {0, 0, 0}, di_flags = 0, di_blocks = 1, di_gen = 1, di_uid = 0, di_gid = 0, di_modrev = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(64));
	struct lfs32_dinode dinode = {
		.di_mode = 16877,
		.di_nlink = 2,
		.di_inumber = 2,
		.di_size = 512,
		.di_atime = 1534531037,
		.di_atimensec = 0,
		.di_mtime = 1534531037,
		.di_mtimensec = 0,
		.di_ctime = 1534531037,
		.di_ctimensec = 0,
		.di_db = {3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = 0,
		.di_blocks = 1,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};
	assert(pwrite(fd, &dinode, sizeof(dinode), off) == sizeof(dinode));
	off += sizeof(dinode);
	}

/*
INODE for ifile:

(gdb) p *(struct lfs32_dinode *)(bp->b_data + sizeof(struct lfs32_dinode))
$61 = {di_mode = 33152, di_nlink = 1, di_inumber = 1, di_size = 40960, di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec = 0, di_ctime = 153453103
7, di_ctimensec = 0, di_db = {5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0}, di_ib = {0, 0, 0}, di_flags = 131072, di_blocks = 5, di_gen = 1, di_uid = 0, di_gid = 0, di_modrev = 0}
*/
	{
	struct lfs32_dinode dinode = {
		.di_mode = 33152,
		.di_nlink = 1,
		.di_inumber = 1,
		.di_size = 40960,
		.di_atime = 1534531037,
		.di_atimensec = 0,
		.di_mtime = 1534531037,
		.di_mtimensec = 0,
		.di_ctime = 1534531037,
		.di_ctimensec = 0,
		.di_db = {5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = 131072,
		.di_blocks = 5,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};
	assert(pwrite(fd, &dinode, sizeof(dinode), off) == sizeof(dinode));
	off += sizeof(dinode);
	}

	/* point to ifile inode */
	lfs.dlfs_idaddr = lfs.dlfs_offset;
	assert(lfs.dlfs_idaddr == 4);

	advance_log(&lfs);
/*
bwrite(blkno=80)

IFILE/CLEANER INFO:

(gdb) p *(struct _cleanerinfo32 *)(bp->b_data)
$134 = {clean = 1022, dirty = 1, bfree = 117367, avail = -6, free_head = 3, free_tail = 408, flags = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(80));
	struct _cleanerinfo32 cleanerinfo = {
		.clean = 1022,
		.dirty = 1,
		.bfree = 117367,
		.avail = -6,
		.free_head = 3,
		.free_tail = 408,
		.flags = 0
	};
	assert(pwrite(fd, &cleanerinfo, sizeof(cleanerinfo), off) == sizeof(cleanerinfo));
	}

	struct segusage empty_seg = {
		.su_nbytes = 0,
		.su_olastmod = 0,
		.su_nsums = 0,
		.su_ninos = 0,
		.su_flags = 16,
		.su_lastmod = 0
	};

	advance_log(&lfs);
/*
bwrite(blkno=96)

IFILE/SEGUSAGE (block 1):

(gdb) p *(struct segusage *)(bp->b_data)
$135 = {su_nbytes = 49408, su_olastmod = 0, su_nsums = 1, su_ninos = 1, su_flags = 7, su_lastmod = 0}

(gdb) p fs->lfs_dlfs_u.u_32.dlfs_sepb
$138 = 341
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(96));
	struct segusage seg = {
		.su_nbytes = 49408,
		.su_olastmod = 0,
		.su_nsums = 1,
		.su_ninos = 1,
		.su_flags = 7,
		.su_lastmod = 0
	};
	assert(pwrite(fd, &seg, sizeof(seg), off) == sizeof(seg));
	off += sizeof(seg);
	int i;
	for (i = 1; i < 341; i++) {
		assert(pwrite(fd, &empty_seg, sizeof(seg), off) == sizeof(seg));
		off += sizeof(seg);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=112)

IFILE/SEGUSAGE (block 2):

(gdb) p *(struct segusage *)(bp->b_data)
$137 = {su_nbytes = 0, su_olastmod = 0, su_nsums = 0, su_ninos = 0, su_flags = 16, su_lastmod = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(112));
	int i;
	for (i = 0; i < 341; i++) {
		assert(pwrite(fd, &empty_seg, sizeof(empty_seg), off) == sizeof(empty_seg));
		off += sizeof(empty_seg);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=128)

IFILE/SEGUSAGE (block 3):

(gdb) p *(struct segusage *)(bp->b_data)
$145 = {su_nbytes = 0, su_olastmod = 0, su_nsums = 0, su_ninos = 0, su_flags = 16, su_lastmod = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(128));
	int i;
	for (i = 0; i < 341; i++) {
		assert(pwrite(fd, &empty_seg, sizeof(empty_seg), off) == sizeof(empty_seg));
		off += sizeof(empty_seg);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=144)

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

	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(144));
	IFILE32 ifile = {
		.if_version = 0,
		.if_daddr = 0,
		.if_nextfree = 0,
		.if_atime_sec = 0,
		.if_atime_nsec = 0
	};
	/* inode 0 (zeroed) */
	assert(pwrite(fd, &ifile, sizeof(ifile), off) == sizeof(ifile));
	off += sizeof(ifile);
	/* inode 1 */
	ifile.if_version = 1;
	assert(pwrite(fd, &ifile, sizeof(ifile), off) == sizeof(ifile));
	off += sizeof(ifile);
	/* inode 2 */
	ifile.if_version = 1;
	ifile.if_daddr = 4;
	assert(pwrite(fd, &ifile, sizeof(ifile), off) == sizeof(ifile));
	off += sizeof(ifile);
	/* inode 3, etc */
	int i;
	for (i = 3; i < DFL_LFSBLOCK / sizeof(ifile); i++) {
		ifile.if_version = 1;
		ifile.if_daddr = 0;
		ifile.if_nextfree = i + 1;
		assert(pwrite(fd, &ifile, sizeof(ifile), off) == sizeof(ifile));
		off += sizeof(ifile);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=16)

SUPERBLOCK:

(gdb) p *(struct dlfs *)bp->b_data
$8 = {.dlfs_magic = 459106, dlfs_version = 2, dlfs_size = 131072, dlfs_ssize = 1048576, dlfs_dsize = 117878, dlfs_bsize = 8192, dlfs_fsize = 8192, dlfs_frag = 1, dlfs_freehd = 3, dlfs_bfree = 117365, dlfs_nfiles = 0, dlfs_avail = -8, dlfs_uinodes = 0, dlfs_idaddr = 4, dlfs_ifile = 1, dlfs_lastseg = 130816, dlfs_nextseg = 128, dlfs_curseg = 0, dlfs_offset = 10, dlfs_lastpseg = 10, dlfs_inopf = 4, dlfs_minfree = 10, dlfs_maxfilesize = 70403120791552, dlfs_fsbpseg = 128, dlfs_inopb = 64, dlfs_ifpb = 409, dlfs_sepb = 341, dlfs_nindir
 = 2048, dlfs_nseg = 1023, dlfs_nspf = 0, dlfs_cleansz = 1, dlfs_segtabsz = 3, dlfs_segmask = 0, dlfs_segshift = 0, dlfs_bshift = 13, dlfs_ffshift = 13, dlfs_fbshift = 0, dlfs_bmask = 8191, dlfs_ffmask = 8191, dlfs_fbmask = 0, dlfs_blktodb = 4, dlfs_sushift = 0, dlfs_maxsymlinklen = 60, dlfs_sboffs = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448, 117504}, dlfs_nclean = 1022, dlfs_fsmnt = '\000' <repeats 89 times>, dlfs_pflags = 1, dlfs_dmeta = 2, dlfs_minfreeseg = 102, dlfs_sumsize = 8192, dlfs_serial = 1, dlfs_ibsize = 8192, dlfs_s0addr = 0, dlfs_tstamp = 0, dlfs_inodefmt = 0, dlfs_interleave = 0, dlfs_ident = 809671122, dlfs_fsbtodb = 4, dlfs_resvseg = 52, dlfs_pad = '\000' <repeats 127 times>, dlfs_cksum = 25346}
*/
	{
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
	assert(lfs.dlfs_idaddr == 4);
	assert(lfs.dlfs_offset == 10);
	assert(lfs.dlfs_lastpseg == 10);
	assert(lfs.dlfs_dmeta == 2);

	//lfs.dlfs_bfree = 117365;
	//lfs.dlfs_avail = -8;

	//lfs.dlfs_serial++;
	//lfs.dlfs_cksum = lfs_sb_cksum32(&lfs);
	//assert(pwrite(fd, &lfs, sizeof(lfs), DFL_LFSBLOCK) == sizeof(lfs));

/*
bwrite(blkno=208896)

SUPERBLOCK:
*/
		
	//lfs.dlfs_cksum = lfs_sb_cksum32(&lfs);
	//assert(pwrite(fd, &lfs, sizeof(lfs), SECTOR_TO_BYTES(208896)) == sizeof(lfs));

	write_superblock(fd, &lfs);
	}

/*
bwrite(blkno=160)

SEGMENT SUMMARY:

$97 = {ss_sumsum = 4092, ss_datasum = 17279, ss_magic = 398689, ss_next = 128, ss_ident = 1618680093, ss_nfinfo = 1, ss_ninos = 1, ss_flags = 8, ss_pad = "\000", ss_reclino
= 0, ss_serial = 2, ss_create = 0}
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(160));
	struct segsum32 segsum = {
		.ss_sumsum = 4092,
		.ss_datasum = 17279,
		.ss_magic = 398689,
		.ss_next = 128,
		.ss_ident = 1618680093,
		.ss_nfinfo = 1,
		.ss_ninos = 1,
		.ss_flags = 8,
		.ss_pad = "\000",
		.ss_reclino= 0,
		.ss_serial = 2,
		.ss_create = 0
	};
	assert(pwrite(fd, &lfs, sizeof(segsum), SECTOR_TO_BYTES(32)) == sizeof(segsum));
	off += sizeof(segsum);

	int ninos = (segsum.ss_ninos + lfs.dlfs_inopb - 1) / lfs.dlfs_inopb;
	lfs.dlfs_dmeta += (lfs.dlfs_sumsize + ninos * lfs.dlfs_ibsize) / DFL_LFSBLOCK;
	}
/*
FINFO 1:

(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32))
$98 = {fi_nblocks = 2, fi_version = 1, fi_ino = 1, fi_lastlength = 8192}
(gdb) p *(int (*)[2])(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32))
$99 = {0, 1}
*/
	{
	int i;
	struct finfo32 finfo = {
		.fi_nblocks = 2,
		.fi_version = 1,
		.fi_ino = 1,
		.fi_lastlength = 8192
	};
	assert(pwrite(fd, &finfo, sizeof(finfo), off) == sizeof(finfo));
	off += sizeof(finfo);
	for (i = 0; i < finfo.fi_nblocks; i++) {
		IINFO32 iinfo = { .ii_block = i };
		assert(pwrite(fd, &iinfo, sizeof(iinfo), off) == sizeof(iinfo));
		off += sizeof(iinfo);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=176)

INODE FOR IFILE:

(gdb) p *(struct lfs32_dinode *)bp->b_data
$100 = {di_mode = 33152, di_nlink = 1, di_inumber = 1, di_size = 40960, di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec = 0, di_ctime = 15345310
37, di_ctimensec = 0, di_db = {12, 13, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0}, di_ib = {0, 0, 0}, di_flags = 131072, di_blocks = 5, di_gen = 1, di_uid = 0, di_gid = 0, di_modrev = 0}

*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(176));
	struct lfs32_dinode dinode = {
		.di_mode = 33152,
		.di_nlink = 1,
		.di_inumber = 1,
		.di_size = 40960,
		.di_atime = 1534531037,
		.di_atimensec = 0,
		.di_mtime = 1534531037,
		.di_mtimensec = 0,
		.di_ctime = 1534531037,
		.di_ctimensec = 0,
		.di_db = {12, 13, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = 131072,
		.di_blocks = 5,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};
	assert(pwrite(fd, &dinode, sizeof(dinode), off) == sizeof(dinode));
	off += sizeof(dinode);
	}

	/* point to ifile inode */
	lfs.dlfs_idaddr = lfs.dlfs_offset;
	assert(lfs.dlfs_idaddr == 11);

	advance_log(&lfs);
/*
bwrite(blkno=192)

CLEANER INFO:

(gdb) p *(struct _cleanerinfo32 *)(bp->b_data)
$125 = {clean = 1022, dirty = 1, bfree = 117870, avail = 124397, free_head = 3, free_tail = 408, flags = 0}
(gdb) x/10 (bp->b_data + sizeof(struct _cleanerinfo32))
0x72844831f01c: 0x              0x              0x              0x
0x72844831f02c: 0x              0x              0x              0x
0x72844831f03c: 0x              0x
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(192));
	struct _cleanerinfo32 cleanerinfo = {
		.clean = 1022,
		.dirty = 1,
		.bfree = 117870,
		.avail = 124397,
		.free_head = 3,
		.free_tail = 408,
		.flags = 0
	};
	assert(pwrite(fd, &cleanerinfo, sizeof(cleanerinfo), off) == sizeof(cleanerinfo));
	}

	advance_log(&lfs);
/*
bwrite(blkno=208)

IFILE/SEGUSAGE (block 1):

(gdb) p *(struct segusage *)(bp->b_data)
$126 = {su_nbytes = 49408, su_olastmod = 0, su_nsums = 2, su_ninos = 2, su_flags = 7, su_lastmod = 0}

(gdb) p fs->lfs_dlfs_u.u_32.dlfs_sepb
$138 = 341
*/
	{
	off = FSBLOCK_TO_BYTES(lfs.dlfs_offset);
	assert(off == SECTOR_TO_BYTES(208));
	struct segusage seg = {
		.su_nbytes = 49408,
		.su_olastmod = 0,
		.su_nsums = 2,
		.su_ninos = 2,
		.su_flags = 7,
		.su_lastmod = 0
	};
	assert(pwrite(fd, &seg, sizeof(seg), off) == sizeof(seg));
	off += sizeof(seg);
	int i;
	for (i = 2; i < 341; i++) {
		assert(pwrite(fd, &empty_seg, sizeof(seg), off) == sizeof(seg));
		off += sizeof(seg);
	}
	}

	advance_log(&lfs);
/*
bwrite(blkno=16)

(gdb) p *(struct dlfs *)bp->b_data
$1 = {dlfs_magic = 459106, dlfs_version = 2, dlfs_size = 131072, dlfs_ssize = 1048576, dlfs_dsize = 117878, dlfs_bsize = 8192, dlfs_fsi
ze = 8192, dlfs_frag = 1, dlfs_freehd = 3, dlfs_bfree = 117868, dlfs_nfiles = 0, dlfs_avail = 124393, dlfs_uinodes = 0, dlfs_idaddr = 1
1, dlfs_ifile = 1, dlfs_lastseg = 130816, dlfs_nextseg = 128, dlfs_curseg = 0, dlfs_offset = 14, dlfs_lastpseg = 14, dlfs_inopf = 4, dl
fs_minfree = 10, dlfs_maxfilesize = 70403120791552, dlfs_fsbpseg = 128, dlfs_inopb = 64, dlfs_ifpb = 409, dlfs_sepb = 341, dlfs_nindir
= 2048, dlfs_nseg = 1023, dlfs_nspf = 0, dlfs_cleansz = 1, dlfs_segtabsz = 3, dlfs_segmask = 0, dlfs_segshift = 0, dlfs_bshift = 13, dl
fs_ffshift = 13, dlfs_fbshift = 0, dlfs_bmask = 8191, dlfs_ffmask = 8191, dlfs_fbmask = 0, dlfs_blktodb = 4, dlfs_sushift = 0, dlfs_max
symlinklen = 60, dlfs_sboffs = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448, 117504}, dlfs_nclean = 1022, dlfs_fsmnt = '
\000' <repeats 89 times>, dlfs_pflags = 1, dlfs_dmeta = 4, dlfs_minfreeseg = 102, dlfs_sumsize = 8192, dlfs_serial = 2, dlfs_ibsize = 8
192, dlfs_s0addr = 0, dlfs_tstamp = 0, dlfs_inodefmt = 0, dlfs_interleave = 0, dlfs_ident = 516731769, dlfs_fsbtodb = 4, dlfs_resvseg =
 52, dlfs_pad = '\000' <repeats 127 times>, dlfs_cksum = 34011}
*/

	assert(lfs.dlfs_idaddr == 11);
	assert(lfs.dlfs_offset == 14);
	assert(lfs.dlfs_lastpseg == 14);
	assert(lfs.dlfs_dmeta == 4);
	assert(lfs.dlfs_avail == 124393);
	//assert(lfs.dlfs_bfree == 117868);

	write_superblock(fd, &lfs);
	return 0;
}
