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
#define MAX_INODES	(DFL_LFSBLOCK / sizeof(IFILE32))

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
	.dlfs_nextseg =		DFL_LFSSEG/DFL_LFSBLOCK,
	.dlfs_curseg =		0,
	.dlfs_nfiles =		0,

	.dlfs_inopf =		512/sizeof(struct lfs32_dinode),
	.dlfs_minfree =		MINFREE,
	.dlfs_maxfilesize =	MAXFILESIZE32,
	.dlfs_fsbpseg =		DFL_LFSSEG/DFL_LFSFRAG,
	.dlfs_inopb =		DFL_LFSBLOCK/sizeof(struct lfs32_dinode),
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
	struct _cleanerinfo32 cleanerinfo;
	SEGUSE *segusage;
	IFILE32	ifiles[MAX_INODES];
};

void init_lfs(struct dlfs *lfs)
{
	uint64_t resvseg;

	nsegs = ((nbytes/DFL_LFSSEG) - 1);
	resvseg = (((nsegs/DFL_MIN_FREE_SEGS) / 2) + 1);

	lfs->dlfs_size = nbytes/DFL_LFSBLOCK;
	lfs->dlfs_dsize = ((uint64_t)(nsegs - nsegs/DFL_MIN_FREE_SEGS) * (uint64_t)DFL_LFSSEG - 
			DFL_LFSBLOCK * (uint64_t)NSUPERBLOCKS) / DFL_LFSBLOCK;
	lfs->dlfs_lastseg = (nbytes - 2*(uint64_t)DFL_LFSSEG) / DFL_LFSBLOCK;
	/* XXX: when nbyes > 19GBs setting bfree confuses whoever is mounting this */
	//lfs->dlfs_bfree = ((nsegs - nsegs/DFL_MIN_FREE_SEGS) * DFL_LFSSEG - 
	//		DFL_LFSBLOCK * NSUPERBLOCKS) / DFL_LFSBLOCK;
	lfs->dlfs_avail = SEGS_TO_FSBLOCKS((nbytes/(uint64_t)DFL_LFSSEG) - resvseg) -
				NSUPERBLOCKS;
	lfs->dlfs_nseg = nsegs;
	lfs->dlfs_segtabsz = ((nsegs + DFL_LFSBLOCK/sizeof(SEGUSE) - 1) /
				(DFL_LFSBLOCK/sizeof(SEGUSE)));
	assert(lfs->dlfs_lastseg <= SEGS_TO_FSBLOCKS(nsegs));
	lfs->dlfs_nclean = nsegs - 1;
	lfs->dlfs_minfreeseg = (nsegs/DFL_MIN_FREE_SEGS);
	lfs->dlfs_resvseg = resvseg;
}

/* Add a block into the data checksum */
void segment_add_datasum(struct segment *seg, char *block, int size)
{
	int i;
	for (i = 0; i < size; i += DFL_LFSBLOCK) {
		/* The final checksum will be done with a sequence of the first
		 * byte of every block */
		memcpy(seg->cur_data_for_cksum, &block[i], sizeof(int32_t));
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
		assert(pwrite64(fd, lfs, sizeof(*lfs),
			FSBLOCK_TO_BYTES(lfs->dlfs_sboffs[i])) == sizeof(*lfs));
		lfs->dlfs_serial++;
	}
	return 0;
}

/* Advance the log by nr FS blocks. */
void _advance_log(struct dlfs *lfs, int nr)
{
	/* Should not be used to make space for a superblock */
	lfs->dlfs_offset += nr;
	lfs->dlfs_lastpseg += nr;
	lfs->dlfs_avail -= nr;
	lfs->dlfs_bfree -= nr;
}

/* Advance the log by nr FS blocks. */
void advance_log_by_one(struct dlfs *lfs, struct segment *seg, struct _ifile *ifile)
{
	//assert(((lfs->dlfs_offset % lfs->dlfs_fsbpseg) + nr) < lfs->dlfs_fsbpseg);
	if (((lfs->dlfs_offset % lfs->dlfs_fsbpseg) + 1) < lfs->dlfs_fsbpseg) {
		_advance_log(lfs, 1);
	} else {
		assert( ((lfs->dlfs_offset + 1) % lfs->dlfs_fsbpseg) == 0 );
		start_segment(lfs, seg, ifile, (char *)seg->segsum);
	}
}

/* Advance the log by nr FS blocks. */
void advance_log(struct dlfs *lfs, struct segment *seg, struct _ifile *ifile, int nr)
{
	int i;
	for (i = 0; i < nr; i++)
		advance_log_by_one(lfs, seg, ifile);
}

void start_segment(struct dlfs *lfs, struct segment *seg, struct _ifile *ifile, char *sb)
{
	struct segsum32 *segsum = (struct segsum32 *)sb;

	++lfs->dlfs_curseg;

	if (lfs->dlfs_curseg == 0) {
		/* The first block of the first segment is kept empty. */
		_advance_log(lfs, 1);
	} else {
		lfs->dlfs_offset = lfs->dlfs_curseg * lfs->dlfs_fsbpseg;
	}

	if (ifile->segusage[lfs->dlfs_curseg].su_flags & SEGUSE_SUPERBLOCK) {
		/* The first block is for the superblock of the segment (if
		 * any) */
		segment_add_datasum(seg, (char*)lfs, DFL_LFSBLOCK);
		_advance_log(lfs, 1);
	}

	seg->fs = (struct lfs*)lfs;
	seg->ninodes = 0;
	seg->seg_bytes_left = lfs->dlfs_ssize;
	seg->sum_bytes_left = lfs->dlfs_sumsize;
	seg->seg_number = lfs->dlfs_curseg;
	seg->disk_bno = lfs->dlfs_offset;
	seg->segsum = (void *)sb;

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

	ifile->segusage[lfs->dlfs_curseg].su_flags |= SEGUSE_ACTIVE|SEGUSE_DIRTY;
	/* One seg. summary per segment. */
	ifile->segusage[lfs->dlfs_curseg].su_nsums = 1;

	/* Make a hole for the segment summary. */
	_advance_log(lfs, lfs->dlfs_sumsize / DFL_LFSBLOCK);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += lfs->dlfs_sumsize;
}

void get_empty_root_dir(char *b)
{
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

	assert(pwrite64(fd, block, sizeof(block),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(block));
	root_bno = lfs->dlfs_offset;
	assert(root_bno == 3);

	segment_add_datasum(seg, (char*)block, DFL_LFSBLOCK);
	advance_log(lfs, seg, ifile, 1);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;

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
	assert(pwrite64(fd, &dinode, sizeof(dinode), off) == sizeof(dinode));

	ifile->ifiles[ULFS_ROOTINO].if_daddr = lfs->dlfs_offset;
	ifile->ifiles[ULFS_ROOTINO].if_nextfree = 0;
	assert(ifile->ifiles[ULFS_ROOTINO].if_daddr == 4);
	ifile->cleanerinfo.free_head++;
	ifile->segusage[lfs->dlfs_curseg].su_ninos++;

	segment_add_datasum(seg, (char *)&dinode, DFL_LFSBLOCK);
	advance_log(lfs, seg, ifile, 1);
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

void init_ifile(struct dlfs *lfs, struct _ifile *ifile)
{
	int i;
	struct segusage empty_segusage = {
		.su_nbytes = 0,
		.su_olastmod = 0,
		.su_nsums = 0,
		.su_ninos = 0,
		.su_flags = SEGUSE_EMPTY,
		.su_lastmod = 0
	};

	/* XXX: Artifial limit on max inodes. */
	//assert(sizeof(ifile->ifiles) <= DFL_LFSBLOCK);

	ifile->segusage = malloc(lfs->dlfs_segtabsz * DFL_LFSBLOCK);
	assert(ifile->segusage);

	memset(&ifile->ifiles[0], 0, sizeof(IFILE32));

	for (i = 1; i < MAX_INODES; i++) {
		ifile->ifiles[i].if_version = 1;
		ifile->ifiles[i].if_daddr = LFS_UNUSED_DADDR;
		ifile->ifiles[i].if_nextfree = i + 1;
		ifile->ifiles[i].if_atime_sec = 0;
		ifile->ifiles[i].if_atime_nsec = 0;
	}

	ifile->cleanerinfo.free_head = 1;
	ifile->cleanerinfo.free_tail = MAX_INODES - 1;

	for (i = 0; i < nsegs; i++) {
		memcpy(&ifile->segusage[i], &empty_segusage,
			sizeof(empty_segusage));
	}
}

void init_sboffs(struct dlfs *lfs, struct _ifile *ifile)
{
	int i, j;
	int sb_interval;	/* number of segs between super blocks */

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

void write_ifile(int fd, struct dlfs *lfs, struct segment *seg, struct _ifile *ifile)
{
	int nblocks = lfs->dlfs_cleansz + lfs->dlfs_segtabsz + 1;
	int32_t first_data_bno;
	int32_t i, bno;
	off_t inode_lbn, indirect_lbn;
	int dinode[DFL_LFSBLOCK / sizeof(int)];
	int db_idx = 0;

	/* TODO: only have single indirect disk blocks */
	assert(nblocks <= ULFS_NDADDR + 1024);
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

	if (nblocks <= ULFS_NDADDR) {
		/* only direct blocks */
		first_data_bno = lfs->dlfs_offset + 1;
		for (i = 0, bno = first_data_bno; i < nblocks; i++, bno++)
			inode.di_db[i] = bno;
	} if (nblocks > ULFS_NDADDR) {
		/* single indirect blocks */
		inode.di_ib[0] = lfs->dlfs_offset + 1;
		first_data_bno = lfs->dlfs_offset + 2;
		for (i = 0, bno = first_data_bno; i < ULFS_NDADDR; i++, bno++)
			inode.di_db[i] = bno;
	}

	assert(inode.di_mode == 33152);
	assert(inode.di_flags == 131072);

	inode_lbn = lfs->dlfs_offset;
	//assert(pwrite64(fd, &inode, sizeof(inode),
	//	FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(inode));

	/* point to ifile inode */
	lfs->dlfs_idaddr = lfs->dlfs_offset;
	assert(lfs->dlfs_idaddr == 5);

	ifile->ifiles[LFS_IFILE_INUM].if_daddr = lfs->dlfs_idaddr;
	ifile->ifiles[LFS_IFILE_INUM].if_nextfree = 0;
	assert(ifile->ifiles[LFS_IFILE_INUM].if_daddr == 5);
	ifile->cleanerinfo.free_head++;
	ifile->segusage[lfs->dlfs_curseg].su_ninos++;

	segment_add_datasum(seg, (char *)&inode, DFL_LFSBLOCK);
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
	advance_log(lfs, seg, ifile, 1);

	if (nblocks > ULFS_NDADDR) {
		/* single indirect blocks */
		inode.di_ib[0] = lfs->dlfs_offset;

		assert(DFL_LFSBLOCK == sizeof(dinode));
		memset(dinode, 0, sizeof(dinode));
		for (i = 0, bno = inode.di_db[ULFS_NDADDR - 1] + 1;
				i < (nblocks - ULFS_NDADDR);
				i++, bno++)
			dinode[i] = bno;

		assert(lfs->dlfs_offset == inode.di_ib[0]);
		indirect_lbn = lfs->dlfs_offset;
		//assert(pwrite64(fd, dinode, 8192,
		//	FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == 8192);

		segment_add_datasum(seg, (char *)dinode, DFL_LFSBLOCK);
		advance_log(lfs, seg, ifile, 1);
		ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
	}

	/* IFILE/CLEANER INFO */
	ifile->cleanerinfo.clean = lfs->dlfs_nclean;
	ifile->cleanerinfo.dirty = lfs->dlfs_curseg + 1;
	ifile->cleanerinfo.bfree = lfs->dlfs_bfree;
	ifile->cleanerinfo.avail = lfs->dlfs_avail;
	assert(ifile->cleanerinfo.dirty == 1);
	assert(ifile->cleanerinfo.free_head == 3);
	assert(ifile->cleanerinfo.free_tail == 408);
	assert(lfs->dlfs_cleansz == 1);

	segment_add_datasum(seg, (char *)&ifile->cleanerinfo, DFL_LFSBLOCK);
	assert(pwrite64(fd, &ifile->cleanerinfo, sizeof(ifile->cleanerinfo),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(ifile->cleanerinfo));
	inode.di_db[db_idx++] = lfs->dlfs_offset;
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK * lfs->dlfs_cleansz;
	advance_log(lfs, seg, ifile, lfs->dlfs_cleansz);

	/* IFILE/SEGUSAGE (block 1) */
	assert(ifile->segusage[lfs->dlfs_curseg].su_nsums == 1);
	assert(ifile->segusage[lfs->dlfs_curseg].su_ninos == 2);
	assert(ifile->segusage[lfs->dlfs_curseg].su_flags == SEGUSE_ACTIVE|SEGUSE_DIRTY|SEGUSE_SUPERBLOCK);
	assert(ifile->segusage[lfs->dlfs_curseg].su_lastmod == 0);
	assert((nsegs * sizeof(SEGUSE)) < (lfs->dlfs_segtabsz * DFL_LFSBLOCK));
	assert(lfs->dlfs_segtabsz == 3);
	for (i = 0; i < lfs->dlfs_segtabsz; i++) {
		segment_add_datasum(seg, (char *)ifile->segusage + i * DFL_LFSBLOCK, DFL_LFSBLOCK);
		assert(pwrite64(fd, (char *)ifile->segusage + i * DFL_LFSBLOCK, 8192,
			FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == 8192);
		inode.di_db[db_idx++] = lfs->dlfs_offset;
		ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
		advance_log(lfs, seg, ifile, 1);
	}

	/* IFILE/INODE MAP */
	segment_add_datasum(seg, (char *)&ifile->ifiles, DFL_LFSBLOCK);
	assert(pwrite64(fd, &ifile->ifiles, sizeof(ifile->ifiles),
		FSBLOCK_TO_BYTES(lfs->dlfs_offset)) == sizeof(ifile->ifiles));
	inode.di_db[db_idx++] = lfs->dlfs_offset;
	ifile->segusage[lfs->dlfs_curseg].su_nbytes += DFL_LFSBLOCK;
	advance_log(lfs, seg, ifile, 1);

	/* Write the inode (and indirect block) */
	assert(pwrite64(fd, &inode, sizeof(inode),
		FSBLOCK_TO_BYTES(inode_lbn)) == sizeof(inode));
	assert(pwrite64(fd, dinode, 8192,
		FSBLOCK_TO_BYTES(indirect_lbn)) == 8192);

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
	size_t sumstart = offsetof(SEGSUM32, ss_datasum);
	struct segsum32 *ssp;
	ssp = (struct segsum32 *)seg->segsum;

	assert(summary_block == (char *)ssp);

	ssp->ss_create = time(0);
	ssp->ss_datasum = cksum(seg->data_for_cksum, (uint64_t)seg->cur_data_for_cksum - (uint64_t)seg->data_for_cksum);
	ssp->ss_sumsum = cksum((char *)seg->segsum + sumstart, lfs->dlfs_sumsize - sumstart);

	assert(pwrite64(fd, summary_block, lfs->dlfs_sumsize,
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
	unsigned char summary_block[lfs.dlfs_sumsize];

	memset(summary_block, 0, lfs.dlfs_sumsize);

	if (argc != 2) {
		errx(1, "Usage: %s <file/device>", argv[0]);
	}

	fd = open(argv[1], O_CREAT | O_RDWR, DEFFILEMODE);
	assert(fd != 0);

	init_lfs(&lfs);

	/* XXX: These make things a lot simpler. */
	assert(DFL_LFSFRAG == DFL_LFSBLOCK);
	assert(lfs.dlfs_fsbpseg > (2 + 6 + 2));
	assert(lfs.dlfs_fsbpseg < MAX_BLOCKS_PER_SEG);
	assert(lfs.dlfs_cleansz == 1);

	init_ifile(&lfs, &ifile);
	init_sboffs(&lfs, &ifile);

	lfs.dlfs_curseg = -1;
	seg.cur_data_for_cksum = &seg.data_for_cksum[0];
	start_segment(&lfs, &seg, &ifile, (char *)&summary_block);

	write_empty_root_dir(fd, &lfs, &seg, &ifile);

	write_ifile(fd, &lfs, &seg, &ifile);

	assert(lfs.dlfs_inopf == 4);
	assert(lfs.dlfs_maxfilesize == 70403120791552);
	assert(lfs.dlfs_fsbpseg == 128);
	assert(lfs.dlfs_fsbtodb == 4);
	assert(lfs.dlfs_cleansz == 1);
	assert(sizeof(CLEANERINFO32) <= DFL_LFSBLOCK);
	assert(sizeof(CLEANERINFO64) <= DFL_LFSBLOCK);
	assert(lfs.dlfs_blktodb == 4);
	assert(lfs.dlfs_sumsize == 8192);
	assert(lfs.dlfs_pflags == 1);
	assert(lfs.dlfs_pflags == LFS_PF_CLEAN);
	assert(lfs.dlfs_nclean = 1022);

	//assert(lfs.dlfs_curseg == 0);
	assert(lfs.dlfs_nextseg == 128);
	assert(lfs.dlfs_idaddr == 5);

	write_superblock(fd, &lfs, (struct segsum32 *)summary_block);

	write_segment_summary(fd, &lfs, &seg, (char *)summary_block);

	return 0;
}
