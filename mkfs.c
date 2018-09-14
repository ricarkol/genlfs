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
#define NSUPERBLOCKS	LFS_MAXNUMSB
#define IFILE_MAP_SZ	1
#define MAX_INODES	((IFILE_MAP_SZ * DFL_LFSBLOCK) / sizeof(IFILE32))

/* globals */
static uint64_t nbytes = (1024 * 1024 * 1 * 1024ull);
static uint64_t nsegs;

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
#define FSBLOCK_TO_BYTES(_S) (DFL_LFSBLOCK * (uint64_t)(_S))
#define SEGS_TO_FSBLOCKS(_S) (((_S) * (uint64_t)DFL_LFSSEG) / DFL_LFSBLOCK)

#define DIV_UP(_x, _y) (((_x) + (_y) - 1) / (_y))
#define MIN(_x, _y) (((_x) < (_y)) ? (_x) : (_y))

static const struct dlfs dlfs32_default = {
	.dlfs_magic =		LFS_MAGIC,
	.dlfs_version =		LFS_VERSION,
	.dlfs_ssize =		DFL_LFSSEG,
	.dlfs_bsize =		DFL_LFSBLOCK,
	.dlfs_fsize =		DFL_LFSFRAG,
	.dlfs_frag =		DFL_LFSBLOCK/DFL_LFSFRAG,
	.dlfs_freehd =		HIGHEST_USED_INO + 1,
	.dlfs_uinodes =		0,
	.dlfs_idaddr =		0,
	.dlfs_ifile =		LFS_IFILE_INUM,
	.dlfs_offset =		0,
	.dlfs_lastpseg =	0,
	.dlfs_nextseg =		0,
	.dlfs_curseg =		0,
	.dlfs_nfiles =		0,

	/* not very efficient, but makes things easier */
	.dlfs_inopf =		1,
	.dlfs_minfree =		MINFREE,
	.dlfs_maxfilesize =	MAXFILESIZE32,
	.dlfs_fsbpseg =		DFL_LFSSEG/DFL_LFSFRAG,
	.dlfs_inopb =		1,
	.dlfs_ifpb =		DFL_LFSBLOCK/sizeof(IFILE32),
	.dlfs_sepb =		DFL_LFSBLOCK/sizeof(SEGUSE),
	.dlfs_nindir =		DFL_LFSBLOCK/sizeof(int32_t),
	.dlfs_nspf =		0,
	.dlfs_cleansz =		1,
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
	.dlfs_fsmnt =   	{ 0 },
	.dlfs_pflags =  	LFS_PF_CLEAN,
	.dlfs_dmeta =		0,
	.dlfs_sumsize =		DFL_LFSFRAG,
	.dlfs_serial =		0,
	.dlfs_ibsize =		DFL_LFSFRAG,
	.dlfs_s0addr =		0,
	.dlfs_tstamp =  	0,
	.dlfs_inodefmt =	LFS_44INODEFMT,
	.dlfs_interleave =	0,
	.dlfs_ident =		0,
	.dlfs_fsbtodb =		LOG2(DFL_LFSBLOCK/512),

	.dlfs_pad = 		{ 0 },
	.dlfs_cksum =		0
};

struct _ifile {
	/*
	 * data holds all the data and cleanerinfo. segusage, and ifiles just
	 * point to it.
	 */
	char *data;
	struct _cleanerinfo32 *cleanerinfo;
	SEGUSE *segusage;
	IFILE32	*ifiles;
};

/* In memory representation of the LFS */
struct fs {
	struct dlfs 	lfs;
	uint32_t	avail_segs;
	struct 		segment seg;
	int		fd;
};

void init_lfs(struct fs *fs)
{
	uint64_t resvseg;
	struct dlfs *lfs = &fs->lfs;

	nsegs = ((nbytes/DFL_LFSSEG) - 1);
	resvseg = (((nsegs/DFL_MIN_FREE_SEGS) / 2) + 1);

	lfs->dlfs_size = nbytes/DFL_LFSBLOCK;
	lfs->dlfs_dsize = ((uint64_t)(nsegs - nsegs/DFL_MIN_FREE_SEGS) *
			(uint64_t)DFL_LFSSEG - 
			DFL_LFSBLOCK * (uint64_t)NSUPERBLOCKS) / DFL_LFSBLOCK;
	lfs->dlfs_lastseg = (nbytes - 2*(uint64_t)DFL_LFSSEG) / DFL_LFSBLOCK;
	lfs->dlfs_bfree = ((nsegs - nsegs/DFL_MIN_FREE_SEGS) * DFL_LFSSEG - 
			DFL_LFSBLOCK * NSUPERBLOCKS) / DFL_LFSBLOCK;
	lfs->dlfs_avail = SEGS_TO_FSBLOCKS((nbytes/(uint64_t)DFL_LFSSEG) - resvseg) -
				NSUPERBLOCKS;
	lfs->dlfs_nseg = nsegs;
	lfs->dlfs_segtabsz = ((nsegs + DFL_LFSBLOCK/sizeof(SEGUSE) - 1) /
				(DFL_LFSBLOCK/sizeof(SEGUSE)));
	assert(lfs->dlfs_lastseg <= SEGS_TO_FSBLOCKS(nsegs));
	lfs->dlfs_nclean = nsegs;
	lfs->dlfs_minfreeseg = (nsegs/DFL_MIN_FREE_SEGS);
	lfs->dlfs_resvseg = resvseg;

	/* This mem is freed at exit time. */
	assert(lfs->dlfs_sumsize >= DFL_LFSBLOCK);
	fs->seg.segsum = malloc(lfs->dlfs_sumsize);
	assert(fs->seg.segsum);
}

/* Add a block into the data checksum */
void segment_add_datasum(struct segment *seg, char *block, uint32_t size)
{
	uint32_t i;
	for (i = 0; i < size; i += DFL_LFSBLOCK) {
		/* The final checksum will be done with a sequence of the first
		 * byte of every block */
		memcpy(seg, &block[i], sizeof(int32_t));
		seg->cur_data_for_cksum++;
	}
}

int write_superblock(struct fs *fs)
{
	struct segsum32 *segsum = fs->seg.segsum;
	uint32_t i;

	for (i = 0; i < NSUPERBLOCKS; i++) {
		fs->lfs.dlfs_cksum = lfs_sb_cksum32(&fs->lfs);
		assert(pwrite64(fs->fd, &fs->lfs, sizeof(fs->lfs),
			FSBLOCK_TO_BYTES(fs->lfs.dlfs_sboffs[i])) == sizeof(fs->lfs));
		fs->lfs.dlfs_serial++;
	}
	return 0;
}

/* Advance the log by nr FS blocks. */
void _advance_log(struct dlfs *lfs, uint32_t nr)
{
	/* Should not be used to make space for a superblock */
	lfs->dlfs_offset += nr;
	lfs->dlfs_lastpseg += nr;
	lfs->dlfs_avail -= nr;
	lfs->dlfs_bfree -= nr;
}

void start_segment(struct fs *fs, struct _ifile *ifile)
{
	struct segsum32 *segsum = fs->seg.segsum;

	assert(segsum != NULL);
	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);

	fs->lfs.dlfs_nclean--;
	fs->lfs.dlfs_curseg += DFL_LFSSEG/DFL_LFSBLOCK;
	fs->lfs.dlfs_nextseg += DFL_LFSSEG/DFL_LFSBLOCK;
	fs->seg.seg_number++;

	printf("start_segment %d %d\n", fs->lfs.dlfs_curseg, fs->lfs.dlfs_nclean);

	if (ifile->segusage[fs->seg.seg_number].su_flags & SEGUSE_SUPERBLOCK) {
		/* The first block is for the superblock of the segment (if
		 * any) */
		segment_add_datasum(&fs->seg, (char*)&fs->lfs, DFL_LFSBLOCK);
		_advance_log(&fs->lfs, 1);
	}

	fs->seg.fs = (struct lfs*)&fs->lfs;
	fs->seg.ninodes = 0;
	fs->seg.seg_bytes_left = fs->lfs.dlfs_ssize;
	fs->seg.sum_bytes_left = fs->lfs.dlfs_sumsize;
	fs->seg.disk_bno = fs->lfs.dlfs_offset;

	/*
	 * We create one segment summary per segment. In other words,
	 * one partial segment per segment.
	 */
	segsum->ss_magic = SS_MAGIC,
	segsum->ss_next = fs->lfs.dlfs_nextseg;
	segsum->ss_ident = 249755386;
	segsum->ss_nfinfo = 0;
	segsum->ss_ninos = 0;
	segsum->ss_flags = SS_RFW;
	segsum->ss_reclino = 0;
	segsum->ss_serial++;

	fs->seg.fip = (FINFO *)((uint64_t)segsum + sizeof(struct segsum32));

	ifile->segusage[fs->seg.seg_number].su_flags |= SEGUSE_ACTIVE|SEGUSE_DIRTY;
	/* One seg. summary per segment. */
	ifile->segusage[fs->seg.seg_number].su_nsums = 1;

	/* Make a hole for the segment summary. */
	_advance_log(&fs->lfs, 1);
	ifile->segusage[fs->seg.seg_number].su_nbytes += fs->lfs.dlfs_sumsize;
	fs->lfs.dlfs_dmeta++;

	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);
}

void write_segment_summary(struct fs *fs)
{
	char *datap, *dp;
	size_t sumstart = offsetof(SEGSUM32, ss_datasum);
	struct segsum32 *ssp;
	ssp = (struct segsum32 *)fs->seg.segsum;

	ssp->ss_create = time(0);
	ssp->ss_datasum = cksum(fs->seg.data_for_cksum,
				((uint64_t)fs->seg.cur_data_for_cksum -
				 (uint64_t)fs->seg.data_for_cksum));
	ssp->ss_sumsum = cksum((char *)fs->seg.segsum + sumstart,
				fs->lfs.dlfs_sumsize - sumstart);

	assert(pwrite64(fs->fd, ssp, DFL_LFSBLOCK,
		FSBLOCK_TO_BYTES(fs->seg.disk_bno)) == DFL_LFSBLOCK);
}

/* Advance the log by nr FS blocks. */
void advance_log_by_one(struct fs *fs, struct _ifile *ifile)
{
	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);
	if ((fs->lfs.dlfs_offset - fs->lfs.dlfs_curseg + 1) < fs->lfs.dlfs_fsbpseg) {
		_advance_log(&fs->lfs, 1);
	} else {
		assert( ((fs->lfs.dlfs_offset + 1) % fs->lfs.dlfs_fsbpseg) == 0 );
		write_segment_summary(fs);
		start_segment(fs, ifile);
	}
	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);
}

/* Advance the log by nr FS blocks. */
void advance_log(struct fs *fs, struct _ifile *ifile, uint32_t nr)
{
	uint32_t i;
	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);
	for (i = 0; i < nr; i++)
		advance_log_by_one(fs, ifile);
	assert(fs->lfs.dlfs_offset >= fs->lfs.dlfs_curseg);
}

/*
 * Sets b (in memory) with the inodes for a . and .. directories.
 */
void get_empty_root_dir(char *b)
{
	struct lfs_dirheader32 dot = {
		.dh_ino = ULFS_ROOTINO,
		.dh_reclen = 12,
		.dh_type = 4,
		.dh_namlen = 1
	};
	memcpy(b, &dot, sizeof(dot));
	b += sizeof(dot);
	memcpy(b, ".", 1);
	b += 4;

	struct lfs_dirheader32 dotdot = {
		.dh_ino = ULFS_ROOTINO,
		.dh_reclen = 500,
		.dh_type = 4,
		.dh_namlen = 2
	};
	memcpy(b, &dotdot, sizeof(dotdot));
	b += sizeof(dotdot);
	memcpy(b, "..", 2);
	b += 4;
/*
	struct lfs_dirheader32 ifile = {
		.dh_ino = 3,
		.dh_reclen = 512 - 24,
		.dh_type = LFS_DT_REG,
		.dh_namlen = strlen("aaaaaaaaaaaaaaax")
	};
	memcpy(b, &ifile, sizeof(ifile));
	b += sizeof(ifile);
	strcpy(b, "aaaaaaaaaaaaaaax");
	b += strlen("aaaaaaaaaaaaaaax");
*/
}

void write_file(struct fs *fs, struct _ifile *ifile,
		char *data, uint64_t size, int inumber,
		int mode, int nlink, int flags);

/*
 * Writes one block for the dir data, and one block for the inode.
 *
 * XXX: One full block for the inode is a bit excessive.
 */
void write_empty_root_dir(struct fs *fs, struct _ifile *ifile)
{
	char block[DFL_LFSBLOCK];

	/* Data for root directory (blkno=48): */
	memset(block, 0, DFL_LFSBLOCK);
	get_empty_root_dir(block);
	
	assert(fs->lfs.dlfs_offset == 3);
	write_file(fs, ifile, block, 512, ULFS_ROOTINO,
		LFS_IFDIR | 0755, 2, 0);
}

void init_ifile(struct dlfs *lfs, struct _ifile *ifile)
{
	uint32_t i;
	struct segusage empty_segusage = {
		.su_nbytes = 0,
		.su_olastmod = 0,
		.su_nsums = 0,
		.su_ninos = 0,
		.su_flags = SEGUSE_EMPTY,
		.su_lastmod = 0
	};

	/* XXX: Artifial limit on max inodes. */
	assert(sizeof(ifile->ifiles) <= DFL_LFSBLOCK);

	uint32_t nblocks = lfs->dlfs_cleansz + lfs->dlfs_segtabsz + IFILE_MAP_SZ;
	ifile->data = malloc(nblocks * DFL_LFSBLOCK);
	assert(ifile->data);

	ifile->cleanerinfo = (struct _cleanerinfo32 *)ifile->data;
	ifile->segusage = (SEGUSE *)(ifile->data + (lfs->dlfs_cleansz * DFL_LFSBLOCK));
	ifile->ifiles = (IFILE32 *)((char *)ifile->segusage + (lfs->dlfs_segtabsz * DFL_LFSBLOCK));

	memset(&ifile->ifiles[0], 0, sizeof(IFILE32));

	for (i = 1; i < MAX_INODES; i++) {
		ifile->ifiles[i].if_version = 1;
		ifile->ifiles[i].if_daddr = LFS_UNUSED_DADDR;
		ifile->ifiles[i].if_nextfree = i + 1;
		ifile->ifiles[i].if_atime_sec = 0;
		ifile->ifiles[i].if_atime_nsec = 0;
	}

	ifile->cleanerinfo->free_head = 1;
	ifile->cleanerinfo->free_tail = MAX_INODES - 1;

	for (i = 0; i < nsegs; i++) {
		memcpy(&ifile->segusage[i], &empty_segusage,
			sizeof(empty_segusage));
	}
}

void init_sboffs(struct dlfs *lfs, struct _ifile *ifile)
{
	uint32_t i, j;
	uint32_t sb_interval;	/* number of segs between super blocks */

	if ((sb_interval = nsegs / LFS_MAXNUMSB) < LFS_MIN_SBINTERVAL)
		sb_interval = LFS_MIN_SBINTERVAL;

	for (i = j = 0; i < nsegs; i++) {
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

void add_finfo_inode(struct fs *fs, uint64_t size, uint32_t inumber)
{
	struct segment *seg = &fs->seg;
	uint32_t nblocks = (size + DFL_LFSBLOCK - 1) / DFL_LFSBLOCK;
	uint32_t lastlength = size % DFL_LFSBLOCK == 0 ?
				DFL_LFSBLOCK : size % DFL_LFSBLOCK;
	struct finfo32 *finfo = (struct finfo32 *)seg->fip;
	uint32_t i;

	assert(size > 0);

	finfo->fi_nblocks = nblocks;
	finfo->fi_version = 1;
	finfo->fi_ino = inumber;
	finfo->fi_lastlength = DFL_LFSBLOCK;
	seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(struct finfo32));
	IINFO32 *blocks = (IINFO32 *)seg->fip;
	for (i = 0; i < finfo->fi_nblocks; i++) {
		blocks[i].ii_block = i;
		seg->fip = (FINFO *)((uint64_t)seg->fip + sizeof(IINFO32));
	}

	assert(((char *)seg->fip - (char *)seg->segsum) <= fs->lfs.dlfs_sumsize);

	((struct segsum32 *)seg->segsum)->ss_ninos++;
	((struct segsum32 *)seg->segsum)->ss_nfinfo++;
}

/* Calculate the number of indirect blocks for a file of size (size) */
uint32_t get_blk_ptrs_nblocks(uint32_t nblocks)
{
	uint32_t res = 1;
	nblocks -= ULFS_NDADDR;

	if (nblocks > (NPTR32 * NPTR32 * NPTR32))
		res += DIV_UP(nblocks, NPTR32 * NPTR32 * NPTR32);
	if (nblocks > (NPTR32 * NPTR32))
		res += DIV_UP(nblocks, NPTR32 * NPTR32);
	if (nblocks > (NPTR32))
		res += DIV_UP(nblocks, NPTR32);
	if (nblocks > 0)
		res += 1;

	return res;
}

/*
 * Writes the block pointers and return the offset of the parent.
 */
int write_single_indirect(struct fs *fs, struct _ifile *ifile,
			int *blk_ptrs, uint32_t nblocks)
{
	uint32_t off = fs->lfs.dlfs_offset;

	assert(nblocks < NPTR32);

	assert(pwrite64(fs->fd, blk_ptrs, DFL_LFSBLOCK,
		FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == DFL_LFSBLOCK);
	segment_add_datasum(&fs->seg, (char *)blk_ptrs, DFL_LFSBLOCK);
	ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
	advance_log(fs, ifile, 1);

	return off;
}

/*
 * Writes the block pointers and return the offset of the parent.
 */
int write_double_indirect(struct fs *fs, struct _ifile *ifile,
			int *blk_ptrs, uint32_t nblocks)
{
	int iblks[NPTR32];
	uint32_t i;
	uint32_t off;
	assert(nblocks < NPTR32 * NPTR32);

	memset(iblks, 0, DFL_LFSBLOCK);

	for (i = 0; nblocks > 0; i++) {
		uint32_t _nblocks = MIN(nblocks, NPTR32);
		assert(i < NPTR32);
		iblks[i] = write_single_indirect(fs, ifile,
						blk_ptrs, _nblocks);
		nblocks -= _nblocks;
		blk_ptrs += _nblocks;
	}

	assert(nblocks == 0);

	off = fs->lfs.dlfs_offset;

	assert(pwrite64(fs->fd, iblks, DFL_LFSBLOCK,
		FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == DFL_LFSBLOCK);
	segment_add_datasum(&fs->seg, (char *)iblks, DFL_LFSBLOCK);
	ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
	advance_log(fs, ifile, 1);

	return off;
}

/*
 * Writes the block pointers and return the offset of the parent.
 */
int write_triple_indirect(struct fs *fs, struct _ifile *ifile,
			int *blk_ptrs, uint32_t nblocks)
{
	int iblks[NPTR32];
	uint32_t i;
	uint32_t off;
	assert(nblocks < NPTR32 * NPTR32 * NPTR32);

	memset(iblks, 0, DFL_LFSBLOCK);

	for (i = 0; nblocks > 0; i++) {
		uint32_t _nblocks = MIN(nblocks, NPTR32 * NPTR32);
		assert(i < NPTR32);
		iblks[i] = write_double_indirect(fs, ifile,
						blk_ptrs, _nblocks);
		nblocks -= _nblocks;
		blk_ptrs += _nblocks;
	}

	assert(nblocks == 0);

	off = fs->lfs.dlfs_offset;

	assert(pwrite64(fs->fd, iblks, DFL_LFSBLOCK,
		FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == DFL_LFSBLOCK);
	segment_add_datasum(&fs->seg, (char *)iblks, DFL_LFSBLOCK);
	ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
	advance_log(fs, ifile, 1);

	return off;
}

void write_file(struct fs *fs, struct _ifile *ifile,
		char *data, uint64_t size, int inumber,
		int mode, int nlink, int flags)
{
	uint32_t nblocks = (size + DFL_LFSBLOCK - 1) / DFL_LFSBLOCK;
	uint32_t i, j;
	int *blk_ptrs;
	int *indirect_blks = malloc(get_blk_ptrs_nblocks(nblocks) * DFL_LFSBLOCK);
	assert(indirect_blks);

	add_finfo_inode(fs, size, inumber);
	assert(fs->lfs.dlfs_inopb == 1);
	fs->lfs.dlfs_dmeta++;

	assert(MAXFILESIZE32 > nblocks * DFL_LFSBLOCK);

	/* Write file inode */
	struct lfs32_dinode inode = {
		.di_mode = mode,
		.di_nlink = nlink,
		.di_inumber = inumber,
		.di_size = size,
		.di_atime = time(0),
		.di_atimensec = 0,
		.di_mtime = time(0),
		.di_mtimensec = 0,
		.di_ctime = time(0),
		.di_ctimensec = 0,
		.di_db = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		.di_ib = {0, 0, 0},
		.di_flags = flags,
		.di_blocks = nblocks,
		.di_gen = 1,
		.di_uid = 0,
		.di_gid = 0,
		.di_modrev = 0
	};

	ifile->cleanerinfo->free_head++;
	ifile->segusage[fs->seg.seg_number].su_ninos++;

	for (i = 0; i < nblocks; i++) {
		char *curr_blk = data + (DFL_LFSBLOCK * i);
		segment_add_datasum(&fs->seg, curr_blk, DFL_LFSBLOCK);
		assert(pwrite64(fs->fd, curr_blk, DFL_LFSBLOCK,
			FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == DFL_LFSBLOCK);
		if (i < ULFS_NDADDR) {
			inode.di_db[i] = fs->lfs.dlfs_offset;
		} else {
			indirect_blks[i - ULFS_NDADDR] = fs->lfs.dlfs_offset;
		}
		ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
		advance_log(fs, ifile, 1);
	}

	nblocks -= MIN(nblocks, ULFS_NDADDR);
	blk_ptrs = indirect_blks;

        if (nblocks > 0) {
                uint32_t _nblocks = MIN(nblocks, NPTR32);
                inode.di_ib[0] = write_single_indirect(fs, ifile,
							blk_ptrs, _nblocks);
                nblocks -= _nblocks;
                blk_ptrs += _nblocks;
        }

        if (nblocks > 0) {
                uint32_t _nblocks = MIN(nblocks, NPTR32 * NPTR32);
                inode.di_ib[1] = write_double_indirect(fs, ifile,
							blk_ptrs, _nblocks);
                nblocks -= _nblocks;
                blk_ptrs += _nblocks;
        }

        if (nblocks > 0) {
                uint32_t _nblocks = MIN(nblocks, NPTR32 * NPTR32 * NPTR32);
                inode.di_ib[2] = write_triple_indirect(fs, ifile,
							blk_ptrs, _nblocks);
                nblocks -= _nblocks;
                blk_ptrs += _nblocks;
        }

	/* Write the inode */
	assert(pwrite64(fs->fd, &inode, sizeof(inode),
		FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == sizeof(inode));
	ifile->ifiles[inumber].if_daddr = fs->lfs.dlfs_offset;
	ifile->ifiles[inumber].if_nextfree = 0;
	segment_add_datasum(&fs->seg, (char *)&inode, DFL_LFSBLOCK);
	ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
	advance_log(fs, ifile, 1);

	free(indirect_blks);
}

/*
 * The difference with write_file is that for an ifile, the inode
 * is written first.
 */
void write_ifile_content(struct fs *fs, struct _ifile *ifile, uint32_t nblocks)
{
	int32_t first_data_bno;
	uint32_t i, bno;
	off_t inode_lbn, indirect_lbn;
	int indirect_blk[DFL_LFSBLOCK / sizeof(int)];
	char *data = ifile->data;
	int inumber = LFS_IFILE_INUM;

	add_finfo_inode(fs, nblocks * DFL_LFSBLOCK, inumber);
	assert(fs->lfs.dlfs_inopb == 1);
	fs->lfs.dlfs_dmeta++;

	/* TODO: only have single indirect disk blocks */
	assert(nblocks <= ULFS_NDADDR + NPTR32);
	assert(MAXFILESIZE32 > nblocks * DFL_LFSBLOCK);

	/* Write ifile inode */
	struct lfs32_dinode inode = {
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

	ifile->cleanerinfo->free_head++;
	ifile->segusage[fs->seg.seg_number].su_ninos++;

	ifile->ifiles[inumber].if_daddr = fs->lfs.dlfs_offset;
	ifile->ifiles[inumber].if_nextfree = 0;
	inode_lbn = fs->lfs.dlfs_offset;
	segment_add_datasum(&fs->seg, (char *)&inode, DFL_LFSBLOCK);
	ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
	advance_log(fs, ifile, 1);

	if (nblocks > ULFS_NDADDR) {
		/* single indirect blocks */
		inode.di_ib[0] = fs->lfs.dlfs_offset;
		indirect_lbn = fs->lfs.dlfs_offset;
		advance_log(fs, ifile, 1);
	}

	for (i = 0; i < nblocks; i++) {
		char *curr_blk = data + (DFL_LFSBLOCK * i);
		segment_add_datasum(&fs->seg, curr_blk, DFL_LFSBLOCK);
		assert(pwrite64(fs->fd, curr_blk, DFL_LFSBLOCK,
			FSBLOCK_TO_BYTES(fs->lfs.dlfs_offset)) == DFL_LFSBLOCK);
		if (i < ULFS_NDADDR) {
			inode.di_db[i] = fs->lfs.dlfs_offset;
		} else {
			indirect_blk[i - ULFS_NDADDR] = fs->lfs.dlfs_offset;
		}
		ifile->segusage[fs->seg.seg_number].su_nbytes += DFL_LFSBLOCK;
		advance_log(fs, ifile, 1);
	}

	/* Write the inode (and indirect block) */
	assert(pwrite64(fs->fd, &inode, sizeof(inode),
		FSBLOCK_TO_BYTES(inode_lbn)) == sizeof(inode));
	if (nblocks > ULFS_NDADDR) {
		/* single indirect block */
		assert(pwrite64(fs->fd, indirect_blk, DFL_LFSBLOCK,
			FSBLOCK_TO_BYTES(indirect_lbn)) == DFL_LFSBLOCK);
	}
}


void write_ifile(struct fs *fs, struct _ifile *ifile)
{
	int nblocks = fs->lfs.dlfs_cleansz + fs->lfs.dlfs_segtabsz + 1;

	/* point to ifile inode */
	fs->lfs.dlfs_idaddr = fs->lfs.dlfs_offset;
	ifile->ifiles[LFS_IFILE_INUM].if_daddr = fs->lfs.dlfs_idaddr;
	ifile->ifiles[LFS_IFILE_INUM].if_nextfree = 0;

	/* IFILE/CLEANER INFO */
	ifile->cleanerinfo->clean = fs->lfs.dlfs_nclean;
	ifile->cleanerinfo->dirty = fs->lfs.dlfs_curseg + 1;
	ifile->cleanerinfo->bfree = fs->lfs.dlfs_bfree;
	ifile->cleanerinfo->avail = fs->lfs.dlfs_avail;
	assert(ifile->cleanerinfo->free_tail == 408);
	assert(fs->lfs.dlfs_cleansz == 1);

	/* IFILE/SEGUSAGE (block 1) */
	assert(ifile->segusage[fs->seg.seg_number].su_nsums == 1);
	assert(ifile->segusage[fs->seg.seg_number].su_lastmod == 0);
	assert((nsegs * sizeof(SEGUSE)) < (fs->lfs.dlfs_segtabsz * DFL_LFSBLOCK));

	/* IFILE/INODE MAP */

	write_ifile_content(fs, ifile, nblocks);
}

int main(int argc, char **argv)
{
	struct fs fs = {
		.lfs = dlfs32_default
	};
	uint32_t avail_segs;
	off_t off;
	struct _ifile ifile;

	if (argc != 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	fs.fd = open(argv[1], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fs.fd != 0);

	init_lfs(&fs.lfs);

	/* XXX: These make things a lot simpler. */
	assert(DFL_LFSFRAG == DFL_LFSBLOCK);
	assert(fs.lfs.dlfs_fsbpseg > (2 + 6 + 2));
	assert(fs.lfs.dlfs_fsbpseg < MAX_BLOCKS_PER_SEG);
	assert(fs.lfs.dlfs_cleansz == 1);

	init_ifile(&fs.lfs, &ifile);
	init_sboffs(&fs.lfs, &ifile);

	fs.lfs.dlfs_curseg = (-1)*(DFL_LFSSEG/DFL_LFSBLOCK);
	fs.seg.seg_number = -1;
	fs.seg.cur_data_for_cksum = &fs.seg.data_for_cksum[0];
	_advance_log(&fs.lfs, 1);
	start_segment(&fs, &ifile);

	write_empty_root_dir(&fs, &ifile);

// passes
//#define FSIZE ((DFL_LFSBLOCK * 113))
// advance_log 125

// fails
//#define FSIZE ((DFL_LFSBLOCK * 114))
//advance_log 126

// fails
//#define FSIZE ((DFL_LFSBLOCK * 115))
//advance_log 127
//start_segment

#define FSIZE ((DFL_LFSBLOCK * 130))
	char *block = malloc(FSIZE);
	assert(block);
	memset(block, '.', FSIZE);

	block[FSIZE - 100] = '\n';
	//write_file(&fs, &ifile, block,
	//		FSIZE, 3, LFS_IFREG | 0777, 1, 0);

	write_ifile(&fs, &ifile);

	write_superblock(&fs);

	write_segment_summary(&fs);

	return 0;
}
