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

#include "lfs.h"
#include "lfs_accessors.h"
#include "config.h"

#define	SF_IMMUTABLE	0x00020000	/* file may not be changed */

#define HIGHEST_USED_INO ULFS_ROOTINO

u_int32_t lfs_sb_cksum32(struct dlfs *fs);

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
	 * We start at block 1, as block 0 is just padding.
	 */
	.dlfs_offset =		1,
	.dlfs_lastpseg =	1,
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

struct _ifile {
	struct _cleanerinfo32 cleanerinfo;
	SEGUSE	segusage[NSEGS];
	IFILE32	ifiles[MAX_INODES];
};

/* Add a block into the data checksum */
void segment_add_datasum(struct segment *seg, char *block, int size)
{
	int i;
	for (i = 0; i < size; i += DFL_LFSBLOCK) {
		/* The final checksum will be done out of the first byte of
		 * every block */
		*seg->cur_data_for_cksum = *((int32_t *)&block[i]);
		seg->cur_data_for_cksum++;
	}
}

int write_superblock(int fd, struct dlfs *lfs, struct segsum32 *segsum)
{
	int i;
	int ninos;

	ninos = (segsum->ss_ninos + lfs->dlfs_inopb - 1) / lfs->dlfs_inopb;
	lfs->dlfs_dmeta += (lfs->dlfs_sumsize + ninos * lfs->dlfs_ibsize) / DFL_LFSBLOCK;

	assert(lfs->dlfs_dmeta == 2);

	for (i = 0; i < NSUPERBLOCKS; i++) {
		lfs->dlfs_cksum = lfs_sb_cksum32(lfs);
		assert(pwrite(fd, lfs, sizeof(*lfs),
			FSBLOCK_TO_BYTES(lfs->dlfs_sboffs[i])) == sizeof(*lfs));
		lfs->dlfs_serial++;
	}
	return 0;
}

/* Advance the log by nr FS blocks. */
void advance_log(struct dlfs *lfs, int nr)
{
	lfs->dlfs_offset += nr;
	lfs->dlfs_lastpseg += nr;
	lfs->dlfs_avail -= nr;
	lfs->dlfs_bfree -= nr;
}

void start_segment(struct dlfs *lfs, struct segment *seg, struct _ifile *ifile, char *sb, bool superblock)
{
	struct segsum32 *segsum = (struct segsum32 *)sb;

	lfs->dlfs_curseg++;

	if (superblock) {
		advance_log(lfs, 1);
		/* We are writing a superblock on this segment. */
		ifile->segusage[lfs->dlfs_curseg].su_flags |= SEGUSE_SUPERBLOCK;
	}

	seg->fs = (struct lfs*)lfs;
	seg->ninodes = 0;
	seg->seg_bytes_left = lfs->dlfs_ssize;
	seg->sum_bytes_left = lfs->dlfs_sumsize;
	seg->seg_number = lfs->dlfs_curseg;
	seg->disk_bno = lfs->dlfs_offset;
	seg->segsum = (void *)sb;
	seg->cur_data_for_cksum = &seg->data_for_cksum[0];

	/*
	 * We create one segment summary per segment. In other words,
	 * one partial segment per segment.
	 */
	segsum->ss_magic = SS_MAGIC,
	segsum->ss_next = lfs->dlfs_nextseg;
	assert(segsum->ss_next == 128);
	segsum->ss_ident = 249755386;
	segsum->ss_nfinfo = 0;
	segsum->ss_ninos = 0;
	segsum->ss_flags = SS_RFW;
	segsum->ss_reclino = 0;
	segsum->ss_serial++;

	seg->fip = (FINFO *)(sb + sizeof(struct segsum32));

	ifile->segusage[lfs->dlfs_curseg].su_flags = SEGUSE_ACTIVE|SEGUSE_DIRTY;
	/* One seg. summary per segment. */
	ifile->segusage[lfs->dlfs_curseg].su_nsums = 1;

	/* Make a hole for the segment summary. */
	advance_log(lfs, lfs->dlfs_sumsize / DFL_LFSBLOCK);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += lfs->dlfs_sumsize;
}

void get_empty_root_dir(char *b)
{
/*
Data for root directory:

(gdb) p *(struct lfs_dirheader32 *)(bp->b_data)
$53 = {dh_ino = 2, dh_reclen = 12, dh_type = 4 '\004', dh_namlen = 1 '\001'}
(gdb) printf "%s\n", (char *)(bp->b_data + sizeof(struct lfs_dirheader32))
.
*/
	{
	struct lfs_dirheader32 dir = {
		.dh_ino = 2,
		.dh_reclen = 12,
		.dh_type = 4,
		.dh_namlen = 1
	};
	memcpy(b, &dir, sizeof(dir));
	b += sizeof(dir);
	char buf[] = ".";
	memcpy(b, &buf, 1);
	b += 4;
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
	memcpy(b, &dir, sizeof(dir));
	b += sizeof(dir);
	char buf[] = "..";
	memcpy(b, &buf, 2);
	b += 4;
	}
}


/*
 * Writes one block for the dir data, and one block for the inode.
 *
 * XXX: One full block for the inode is a bit excessive.
 */
void write_empty_root_dir(int fd, struct dlfs *lfs, struct segment *seg, struct _ifile *ifile)
{
	char block[DFL_LFSBLOCK];
	off_t root_bno; /* root dir data block number. */
	off_t off;

	/* Data for root directory (blkno=48): */
	memset(block, 0, DFL_LFSBLOCK);
	get_empty_root_dir(block);

	assert(pwrite(fd, block, sizeof(block),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(block));
	root_bno = lfs->dlfs_offset;
	assert(root_bno == 3);

	segment_add_datasum(seg, (char*)block, DFL_LFSBLOCK);
	advance_log(lfs, 1);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
/*
bwrite(blkno=64)

INODE for root directory:

(gdb) p *(struct lfs32_dinode *)bp->b_data
$60 = {di_mode = 16877, di_nlink = 2, di_inumber = 2, di_size = 512, di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec = 0, di_ctime = 1534531037,
 di_ctimensec = 0, di_db = {3, 0 <repeats 11 times>}, di_ib = {0, 0, 0}, di_flags = 0, di_blocks = 1, di_gen = 1, di_uid = 0, di_gid = 0, di_modrev = 0}
*/
	off = FSBLOCK_TO_BYTES(lfs->dlfs_offset);
	assert(off == SECTOR_TO_BYTES(64));
	struct lfs32_dinode dinode = {
		.di_mode = LFS_IFDIR | 0755,
		.di_nlink = 2,
		.di_inumber = ULFS_ROOTINO,
		.di_size = 512,
		.di_atime = time(0),
		.di_atimensec = 0,
		.di_mtime = time(0),
		.di_mtimensec = 0,
		.di_ctime = time(0),
		.di_ctimensec = 0,
		.di_db = {root_bno, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = 0,
		.di_blocks = 1,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};
	assert(dinode.di_mode == 16877);
	assert(pwrite(fd, &dinode, sizeof(dinode), off) == sizeof(dinode));

	ifile->ifiles[ULFS_ROOTINO].if_daddr = lfs->dlfs_offset;
	ifile->ifiles[ULFS_ROOTINO].if_nextfree = 0;
	assert(ifile->ifiles[ULFS_ROOTINO].if_daddr == 4);
	ifile->cleanerinfo.free_head++;
	ifile->segusage[lfs->dlfs_curseg].su_ninos++;

	segment_add_datasum(seg, (char *)&dinode, DFL_LFSBLOCK);
	advance_log(lfs, 1);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;

	struct finfo32 *finfo = (struct finfo32 *)seg->fip;
	finfo->fi_nblocks = 1;
	finfo->fi_version = 1;
	finfo->fi_ino = ULFS_ROOTINO;
	finfo->fi_lastlength = DFL_LFSBLOCK;
	seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(struct finfo32));
	IINFO32 *blocks = (IINFO32 *)seg->fip;
	int i;
	for (i = 0; i < finfo->fi_nblocks; i++) {
		blocks[i].ii_block = i;
		seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(IINFO32));
	}
	((struct segsum32 *)seg->segsum)->ss_ninos++;
	((struct segsum32 *)seg->segsum)->ss_nfinfo++;
}

void init_ifile(struct _ifile *ifile)
{
	struct segusage empty_segusage = {
		.su_nbytes = 0,
		.su_olastmod = 0,
		.su_nsums = 0,
		.su_ninos = 0,
		.su_flags = SEGUSE_EMPTY,
		.su_lastmod = 0
	};
	int i;

	/* XXX: Artifial limit on max inodes. */
	assert(sizeof(ifile->ifiles) <= DFL_LFSBLOCK);

	memset(&ifile->ifiles[0], 0, sizeof(IFILE32));

	for (i = 1; i < MAX_INODES; i++) {
		ifile->ifiles[i].if_version = 1;
		ifile->ifiles[i].if_daddr = LFS_UNUSED_DADDR;
		ifile->ifiles[i].if_nextfree = i + 1;
	}

	ifile->cleanerinfo.free_head = 1;
	ifile->cleanerinfo.free_tail = MAX_INODES - 1;

	for (i = 0; i < NSEGS; i++) {
		memcpy(&ifile->segusage[i], &empty_segusage,
			sizeof(empty_segusage));
	}
}

void init_sboffs(struct dlfs *lfs, struct _ifile *ifile)
{
	int i, j;
	int sb_interval;	/* number of segs between super blocks */

	if ((sb_interval = NSEGS / LFS_MAXNUMSB) < LFS_MIN_SBINTERVAL)
		sb_interval = LFS_MIN_SBINTERVAL;

	for (i = j = 0; i < NSEGS; i++) {
		if (i == 0) {
			ifile->segusage[i].su_flags = SEGUSE_SUPERBLOCK;
			lfs->dlfs_sboffs[j] = 1;
			++j;
		}
		if (i > 0) {
			if ((i % sb_interval) == 0 && j < LFS_MAXNUMSB) {
				ifile->segusage[i].su_flags = SEGUSE_SUPERBLOCK;
				lfs->dlfs_sboffs[j] = i * lfs->dlfs_fsbpseg;
				++j;
			}
		}
	}
}

void write_ifile(int fd, struct dlfs *lfs, struct segment *seg, struct _ifile *ifile)
{

/*
INODE for ifile:

(gdb) p *(struct lfs32_dinode *)(bp->b_data + sizeof(struct lfs32_dinode))
$61 = {di_mode = 33152, di_nlink = 1, di_inumber = 1, di_size = 40960, di_atime = 1534531037, di_atimensec = 0, di_mtime = 1534531037, di_mtimensec = 0, di_ctime = 153453103
7, di_ctimensec = 0, di_db = {5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0}, di_ib = {0, 0, 0}, di_flags = 131072, di_blocks = 5, di_gen = 1, di_uid = 0, di_gid = 0, di_modrev = 0}
*/
	// TODO: check that there are enough blocks in this segment for the ifile

	int nblocks = lfs->dlfs_cleansz + lfs->dlfs_segtabsz + 1;
	assert(nblocks == 5);
	int32_t first_data_bno = lfs->dlfs_offset + 1; /* one after this inode */
	int32_t i, bno;

	/* TODO: implement indirect disk blocks */
	assert(nblocks <= ULFS_NDADDR);

	struct lfs32_dinode dinode = {
		.di_mode = LFS_IFREG | 0600,
		.di_nlink = 1,
		.di_inumber = LFS_IFILE_INUM,
		.di_size = nblocks * DFL_LFSBLOCK,
		.di_atime = time(0),
		.di_atimensec = 0,
		.di_mtime = time(0),
		.di_mtimensec = 0,
		.di_ctime = time(0),
		.di_ctimensec = 0,
		.di_db = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = SF_IMMUTABLE,
		.di_blocks = nblocks,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};
	for (i = 0, bno = first_data_bno; i < nblocks; i++, bno++)
		dinode.di_db[i] = bno;

	assert(dinode.di_mode == 33152);
	assert(dinode.di_flags == 131072);

	assert(pwrite(fd, &dinode, sizeof(dinode),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(dinode));

	/* point to ifile inode */
	lfs->dlfs_idaddr = lfs->dlfs_offset;
	assert(lfs->dlfs_idaddr == 5);

	ifile->ifiles[LFS_IFILE_INUM].if_daddr = lfs->dlfs_idaddr;
	ifile->ifiles[LFS_IFILE_INUM].if_nextfree = 0;
	assert(ifile->ifiles[LFS_IFILE_INUM].if_daddr == 5);
	ifile->cleanerinfo.free_head++;
	ifile->segusage[lfs->dlfs_curseg].su_ninos++;

	segment_add_datasum(seg, (char *)&dinode, DFL_LFSBLOCK);
	advance_log(lfs, 1);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
/*
bwrite(blkno=80)

IFILE/CLEANER INFO:

(gdb) p *(struct _cleanerinfo32 *)(bp->b_data)
$134 = {clean = 1022, dirty = 1, bfree = 117367, avail = -6, free_head = 3, free_tail = 408, flags = 0}
*/
	ifile->cleanerinfo.clean = lfs->dlfs_nclean,
	ifile->cleanerinfo.dirty = lfs->dlfs_curseg + 1,
	ifile->cleanerinfo.bfree = lfs->dlfs_bfree,
	ifile->cleanerinfo.avail = lfs->dlfs_avail,
	assert(ifile->cleanerinfo.clean == 1022);
	assert(ifile->cleanerinfo.dirty == 1);
	assert(ifile->cleanerinfo.free_head == 3);
	assert(ifile->cleanerinfo.free_tail == 408);
	assert(pwrite(fd, &ifile->cleanerinfo, sizeof(ifile->cleanerinfo),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(ifile->cleanerinfo));

	segment_add_datasum(seg, (char *)&ifile->cleanerinfo, DFL_LFSBLOCK);
	advance_log(lfs, lfs->dlfs_cleansz);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK * lfs->dlfs_cleansz;
/*
bwrite(blkno=96)

IFILE/SEGUSAGE (block 1):

(gdb) p *(struct segusage *)(bp->b_data)
$135 = {su_nbytes = 49408, su_olastmod = 0, su_nsums = 1, su_ninos = 1, su_flags = 7, su_lastmod = 0}

(gdb) p fs->lfs_dlfs_u.u_32.dlfs_sepb
$138 = 341
*/
	assert(ifile->segusage[lfs->dlfs_curseg].su_nsums == 1);
	assert(ifile->segusage[lfs->dlfs_curseg].su_ninos == 2);
	//assert(ifile->segusage[lfs->dlfs_curseg].su_nbytes == 49408);
	assert(ifile->segusage[lfs->dlfs_curseg].su_flags == SEGUSE_ACTIVE|SEGUSE_DIRTY|SEGUSE_SUPERBLOCK);
	assert(ifile->segusage[lfs->dlfs_curseg].su_lastmod == 0);

	assert(pwrite(fd, ifile->segusage, sizeof(ifile->segusage),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(ifile->segusage));

	segment_add_datasum(seg, (char *)ifile->segusage, lfs->dlfs_segtabsz * DFL_LFSBLOCK);
	advance_log(lfs, lfs->dlfs_segtabsz);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK * lfs->dlfs_segtabsz;

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

	assert(pwrite(fd, &ifile->ifiles, sizeof(ifile->ifiles),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(ifile->ifiles));

	segment_add_datasum(seg, (char *)&ifile->ifiles, DFL_LFSBLOCK);
	advance_log(lfs, 1);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;

	struct finfo32 *finfo = (struct finfo32 *)seg->fip;
	finfo->fi_nblocks = nblocks;
	finfo->fi_version = 1;
	finfo->fi_ino = LFS_IFILE_INUM;
	finfo->fi_lastlength = DFL_LFSBLOCK;
	seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(struct finfo32));
	IINFO32 *blocks = (IINFO32 *)seg->fip;
	for (i = 0; i < finfo->fi_nblocks; i++) {
		blocks[i].ii_block = i;
		seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(IINFO32));
	}
	((struct segsum32 *)seg->segsum)->ss_ninos++;
	((struct segsum32 *)seg->segsum)->ss_nfinfo++;
}

void write_segment_summary(int fd, struct dlfs *lfs, struct segment *seg, char *summary_block)
{
	char *datap, *dp;

	((struct segsum32 *)seg->segsum)->ss_create = time(0);

	//assert(((struct segsum32 *)seg->segsum)->ss_datasum == 33555);

	assert(pwrite(fd, summary_block, lfs->dlfs_sumsize,
		FSBLOCK_TO_BYTES(seg->disk_bno)) == lfs->dlfs_sumsize);
}

int main(int argc, char **argv)
{
	struct dlfs lfs = dlfs32_default;
	int fd;
	uint32_t avail_segs;
	off_t off;
	struct segment seg;
	struct _ifile ifile;
	char summary_block[lfs.dlfs_sumsize];

	if (argc != 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	fd = open(argv[1], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fd != 0);

	/* XXX: These make things a lot simpler. */
	assert(DFL_LFSFRAG == DFL_LFSBLOCK);
	assert(lfs.dlfs_fsbpseg > (2 + 6 + 2));
	assert(lfs.dlfs_fsbpseg < MAX_BLOCKS_PER_SEG);
	assert(lfs.dlfs_cleansz == 1);
	assert(lfs.dlfs_segtabsz == 3);

	init_ifile(&ifile);
	init_sboffs(&lfs, &ifile);

	lfs.dlfs_curseg = -1;
	start_segment(&lfs, &seg, &ifile, (char *)&summary_block, true);

	write_empty_root_dir(fd, &lfs, &seg, &ifile);

	write_ifile(fd, &lfs, &seg, &ifile);

/*
bwrite(blkno=16)

SUPERBLOCK:

(gdb) p *(struct dlfs *)bp->b_data
$8 = {.dlfs_magic = 459106, dlfs_version = 2, dlfs_size = 131072, dlfs_ssize = 1048576, dlfs_dsize = 117878, dlfs_bsize = 8192, dlfs_fsize = 8192, dlfs_frag = 1, dlfs_freehd = 3, dlfs_bfree = 117365, dlfs_nfiles = 0, dlfs_avail = -8, dlfs_uinodes = 0, dlfs_idaddr = 4, dlfs_ifile = 1, dlfs_lastseg = 130816, dlfs_nextseg = 128, dlfs_curseg = 0, dlfs_offset = 10, dlfs_lastpseg = 10, dlfs_inopf = 4, dlfs_minfree = 10, dlfs_maxfilesize = 70403120791552, dlfs_fsbpseg = 128, dlfs_inopb = 64, dlfs_ifpb = 409, dlfs_sepb = 341, dlfs_nindir
 = 2048, dlfs_nseg = 1023, dlfs_nspf = 0, dlfs_cleansz = 1, dlfs_segtabsz = 3, dlfs_segmask = 0, dlfs_segshift = 0, dlfs_bshift = 13, dlfs_ffshift = 13, dlfs_fbshift = 0, dlfs_bmask = 8191, dlfs_ffmask = 8191, dlfs_fbmask = 0, dlfs_blktodb = 4, dlfs_sushift = 0, dlfs_maxsymlinklen = 60, dlfs_sboffs = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448, 117504}, dlfs_nclean = 1022, dlfs_fsmnt = '\000' <repeats 89 times>, dlfs_pflags = 1, dlfs_dmeta = 2, dlfs_minfreeseg = 102, dlfs_sumsize = 8192, dlfs_serial = 1, dlfs_ibsize = 8192, dlfs_s0addr = 0, dlfs_tstamp = 0, dlfs_inodefmt = 0, dlfs_interleave = 0, dlfs_ident = 809671122, dlfs_fsbtodb = 4, dlfs_resvseg = 52, dlfs_pad = '\000' <repeats 127 times>, dlfs_cksum = 25346}
*/
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
	//assert(lfs.dlfs_offset == 10);
	//assert(lfs.dlfs_lastpseg == 10);

	//lfs.dlfs_bfree = 117365;
	//lfs.dlfs_avail = -8;

	int offs[] = {1, 13056, 26112, 39168, 52224, 65280, 78336, 91392, 104448, 117504};
	assert(lfs.dlfs_sboffs[0] == offs[0]);
	assert(lfs.dlfs_sboffs[3] == offs[3]);
	assert(lfs.dlfs_sboffs[5] == offs[5]);
	assert(lfs.dlfs_sboffs[8] == offs[8]);

/*
bwrite(blkno=208896)

SUPERBLOCK:
*/
	write_superblock(fd, &lfs, (struct segsum32 *)summary_block);

/*
bwrite(blkno=32)

SEGMENT SUMMARY:

(gdb) p *(struct segsum32 *)bp->b_data
$31 = {ss_sumsum = 28386, ss_datasum = 33555, ss_magic = 398689, ss_next = 128, ss_ident = 249755386, ss_nfinfo = 2, ss_ninos = 2, ss_f
lags = 8, ss_pad = "\000", ss_reclino = 0, ss_serial = 1, ss_create = 0}

FINFO 1:

(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32))
$5 = {fi_nblocks = 1, fi_version = 1, fi_ino = 2, fi_lastlength = 8192}
(gdb) p *(int *)(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32))
$28 = 0

FINFO 2:

(gdb) p *(struct finfo32 *)(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int))
$30 = {fi_nblocks = 5, fi_version = 1, fi_ino = 1, fi_lastlength = 8192}
(gdb) p *(int (*)[5])(bp->b_data + sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int) + sizeof(struct finfo32))
$31 = {0, 1, 2, 3, 4}
*/
	assert(((struct segsum32 *)seg.segsum)->ss_magic == 398689);
	assert(((struct segsum32 *)seg.segsum)->ss_next == 128);
	assert(((struct segsum32 *)seg.segsum)->ss_nfinfo == 2);
	assert(((struct segsum32 *)seg.segsum)->ss_ninos == 2);
	assert(((struct segsum32 *)seg.segsum)->ss_serial == 1);
	assert(FSBLOCK_TO_BYTES(seg.disk_bno) == SECTOR_TO_BYTES(32));
	struct finfo32 *finfo1 = (struct finfo32 *)(summary_block +
		sizeof(struct segsum32));
	assert(finfo1->fi_ino == 2);
	struct finfo32 *finfo2 = (struct finfo32 *)(summary_block +
		sizeof(struct segsum32) + sizeof(struct finfo32) + sizeof(int));
	assert(finfo2->fi_ino == 1);
	assert(*(summary_block + sizeof(struct segsum32) +
		sizeof(struct finfo32) + sizeof(int) +
		sizeof(struct finfo32) + sizeof(int) + sizeof(int)) == 2);

	write_segment_summary(fd, &lfs, &seg, (char *)summary_block);

	return 0;
}
