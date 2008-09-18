#include <string.h>

#include "punix.h"
#include "proc.h"
#include "buf.h"
#include "dev.h"
#include "fs.h"
#include "inode.h"
#include "mount.h"
#include "queue.h"
#include "globals.h"

/* FIXME: from v7. fix it up yo */

#define prdev(s, dev) ((void)0)

typedef	struct fblk *FBLKP;

#if 0 /* re-write these to work with the Punix flash fs */
/*
 * alloc will obtain the next available
 * free disk block from the free list of
 * the specified device.
 * The super block has up to NICFREE remembered
 * free blocks; the last of these is read to
 * obtain NICFREE more . . .
 *
 * no space on dev x/y -- when
 * the free list is exhausted.
 */
struct buf *alloc(dev_t dev)
{
	long bno;
	struct fs *fp;
	struct buf *bp;
	
	fp = getfs(dev);
	while (fp->fs_flock)
		slp(&fp->fs_flock, PINOD);
	do {
		if (fp->fs_nfree <= 0)
			goto nospace;
		if (fp->fs_nfree > NICFREE) {
			prdev("Bad free count", dev);
			goto nospace;
		}
		bno = fp->fs_free[--fp->fs_nfree];
		if (bno == 0)
			goto nospace;
	} while (badblock(fp, bno, dev));
	if (fp->fs_nfree <= 0) {
		++fp->fs_flock;
		bp = bread(dev, bno);
		if ((bp->b_flags&B_ERROR) == 0) {
			fp->fs_nfree = ((FBLKP)(bp->b_un.b_addr))->df_nfree;
			memcpy(fp->fs_free, ((FBLKP)(bp->b_un.b_addr))->df_free,
			       sizeof(fp->fs_free));
		}
		brelse(bp);
		fp->fs_flock = 0;
		wakeup((caddr_t)&fp->fs_flock);
		if (fp->fs_nfree <= 0)
			goto nospace;
	}
	bp = getblk(dev, bno);
	clrbuf(bp);
	fp->fs_fmod = 1;
	return bp;
	
nospace:
	fp->fs_nfree = 0;
	prdev("no space", dev);
	u.u_error = ENOSPC;
	return NULL;
}

/*
 * place the specified disk block
 * back on the free list of the
 * specified device.
 */
void free(dev_t dev, long bno)
{
	struct fs *fp;
	struct buf *bp;
	
	fp = getfs(dev);
	fp->fs_fmod = 1;
	while (fp->fs_flock)
		slp(&fp->fs_flock, PINOD);
	if (badblock(fp, bno, dev))
		return;
	if(fp->fs_nfree <= 0) {
		fp->fs_nfree = 1;
		fp->fs_free[0] = 0;
	}
	if (fp->fs_nfree >= NICFREE) {
		++fp->fs_flock;
		bp = getblk(dev, bno);
		((FBLKP)(bp->b_un.b_addr))->df_nfree = fp->fs_nfree;
		memcpy(((FBLKP)(bp->b_un.b_addr))->df_free, fp->fs_free,
			sizeof(fp->fs_free));
		fp->fs_nfree = 0;
		bwrite(bp);
		fp->fs_flock = 0;
		wakeup(&fp->fs_flock);
	}
	fp->fs_free[fp->fs_nfree++] = bno;
	fp->fs_fmod = 1;
}

/*
 * Check that a block number is in the
 * range between the I list and the size
 * of the device.
 * This is used mainly to check that a
 * garbage file system has not been mounted.
 *
 * bad block on dev x/y -- not in range
 */
int badblock(struct fs *fp, long bn, dev_t dev)
{
	if (bn < fp->fs_isize || bn >= fp->fs_fsize) {
		prdev("bad block", dev);
		return 1;
	}
	return 0;
}
#endif

/*
 * Allocate an unused I node
 * on the specified device.
 * Used with file creation.
 * The algorithm keeps up to
 * NICINOD spare I nodes in the
 * super block. When this runs out,
 * a linear search through the
 * I list is instituted to pick
 * up NICINOD more.
 */
struct inode *ialloc(dev_t dev)
{
	return NULL;
#if 0
	struct fs *fp;
	struct buf *bp;
	struct inode *ip;
	int i;
	struct dinode *dp;
	ino_t ino;
	daddr_t adr;
	
	fp = getfs(dev);
	while (fp->fs_ilock)
		slp(&fp->fs_ilock, PINOD);
loop:
	if (fp->fs_ninode > 0) {
		ino = fp->fs_inode[--fp->fs_ninode];
		if (ino < ROOTINO)
			goto loop;
		ip = iget(dev, ino);
		if (ip == NULL)
			return NULL;
		if (ip->i_mode == 0) {
			for (i=0; i<NADDR; ++i)
				ip->i_un.i_addr[i] = 0;
			fp->fs_fmod = 1;
			return(ip);
		}
		/*
		 * Inode was allocated after all.
		 * Look some more.
		 */
		iput(ip);
		goto loop;
	}
	fp->fs_ilock++;
	ino = 1;
	for(adr = SUPERB+1; adr < fp->fs_isize; adr++) {
		bp = bread(dev, adr);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			ino += INOPB;
			continue;
		}
		dp = bp->b_un.b_dino;
		for(i=0; i<INOPB; i++) {
			if(dp->di_mode != 0)
				goto cont;
			for EACHINODE(ip)
				if (dev==ip->i_dev && ino==ip->i_number)
					goto cont;
			fp->fs_inode[fp->fs_ninode++] = ino;
			if(fp->fs_ninode >= NICINOD)
				break;
		cont:
			ino++;
			dp++;
		}
		brelse(bp);
		if(fp->fs_ninode >= NICINOD)
			break;
	}
	fp->fs_ilock = 0;
	wakeup((caddr_t)&fp->fs_ilock);
	if(fp->fs_ninode > 0)
		goto loop;
	prdev("Out of inodes", dev);
	u.u_error = ENOSPC;
	return(NULL);
#endif
}

/*
 * Free the specified I node
 * on the specified device.
 * The algorithm stores up
 * to NICINOD I nodes in the super
 * block and throws away any more.
 */
void ifree(dev_t dev, ino_t ino)
{
#if 0
	register struct fs *fp;

	fp = getfs(dev);
	if (fp->fs_ilock)
		return;
	if (fp->fs_ninode >= NICINOD)
		return;
	fp->fs_inode[fp->fs_ninode++] = ino;
	fp->fs_fmod = 1;
#endif
}

/*
 * getfs maps a device number into
 * a pointer to the incore super
 * block.
 * The algorithm is a linear
 * search through the mount table.
 * A consistency check of the
 * in core free-block and i-node
 * counts.
 *
 * bad count on dev x/y -- the count
 *	check failed. At this point, all
 *	the counts are zeroed which will
 *	almost certainly lead to "no space"
 *	diagnostic
 * panic: no fs -- the device is not mounted.
 *	this "cannot happen"
 */
struct fs *getfs(dev_t dev)
{
	struct mount *mp;
	
	for EACHMOUNT(mp) {
		if (mp->m_filsys != NULL && mp->m_dev == dev) {
			struct fs *fp = mp->m_filsys;
			if (fp->fs_nfree > NICFREE || fp->fs_ninode > NICINOD) {
				prdev("bad count", dev);
				fp->fs_nfree = 0;
				fp->fs_ninode = 0;
			}
			return fp;
		}
	}
	panic("no fs");
	return NULL;
}

/*
 * update is the internal name of
 * 'sync'. It goes through the disk
 * queues to initiate sandbagged IO;
 * goes through the I nodes to write
 * modified nodes; and it goes through
 * the mount table to initiate modified
 * super blocks.
 */
void update()
{
	register struct inode *ip;
	register struct mount *mp;
	register struct buf *bp;
	struct fs *fp;
	
	if (updlock)
		return;
	
	++updlock;
	
	for EACHMOUNT(mp)
		if (mp->m_filsys != NULL) {
			fp = mp->m_filsys;
			if (fp->fs_fmod == 0 || fp->fs_ilock != 0 ||
			    fp->fs_flock != 0 || fp->fs_ronly != 0)
				continue;
			
			bp = getblk(mp->m_dev, SUPERB);
			
			if (bp->b_flags & B_ERROR)
				continue;
			
			fp->fs_fmod = 0;
			fp->fs_time = walltime.tv_sec;
			memcpy(bp->b_addr, fp, BSIZE);
			bwrite(bp);
		}
	
	for EACHINODE(ip)
		if ((ip->i_flag & ILOCKED) == 0 && ip->i_count != 0) {
			ip->i_flag |= ILOCKED;
			++ip->i_count;
			iupdat(ip, &walltime.tv_sec, &walltime.tv_sec);
			iput(ip);
		}
	
	updlock = 0;
	bflush(NODEV);
}