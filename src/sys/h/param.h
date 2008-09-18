/*
 * tunable variables
 */

#define	NBUF	29		/* size of buffer cache */
#define	NINODE	200		/* number of in core inodes */
#define	NFILE	175		/* number of in core file structures */
#define	NMOUNT	8		/* number of mountable file systems */
#define	MAXMEM	(64*32)		/* max core per process - first # is Kw */
#define	MAXUPRC	25		/* max processes per user */
#define	SSIZE	20		/* initial stack size (*64 bytes) */
#define	SINCR	20		/* increment of stack (*64 bytes) */
#define	NOFILE	20		/* max open files per process */
#define	CANBSIZ	256		/* max size of typewriter line */
#define	CMAPSIZ	50		/* size of core allocation area */
#define	SMAPSIZ	50		/* size of swap allocation area */
#define	NCALL	20		/* max simultaneous time callouts */
#define	NPROC	150		/* max number of processes */
#define	NGROUPS	16		/* max number of groups */
#define	NOGROUP	(-1)		
#define	NTEXT	40		/* max number of pure texts */
#define	NCLIST	100		/* max total clist size */
#define	HZ	256		/* XXX: Ticks/second of the clock */
#define TICK	(1000000000L / HZ) /* nanoseconds/tick of the clock */

#define	TIMEZONE (5*60)		/* Minutes westward from Greenwich */
#define	DSTFLAG	1		/* Daylight Saving Time applies in this locality */
#define	MSGBUFS	128		/* Characters saved from error messages */
#if 0 /* use MAX_ARGS */
#define	NCARGS	5120		/* # characters in exec arglist */
#endif

#define MAXSYMLINKS 6
#define MAXPATHLEN 256

#define QUANTUM (HZ/32)

#define CPUSCALE      64 /* give the cpu time some extra resolution */
#define CPUMAX        (CPUSCALE * HZ / 4)
#define CPUDECAY      (CPUSCALE * 13 / 16)
#define CPUPRIWEIGHT  1
#define NICEPRIWEIGHT ((CPUMAX - QUANTUM * CPUSCALE) / 20)

/*
 * priorities
 * probably should not be
 * altered too much
 */

#define	PSWP	0
#define	PINOD	10
#define PAUDIO	10 /* XXX */
#define	PRIBIO	20
#define	PZERO	25
#define	NZERO	20
#define	PPIPE	26
#define	PWAIT	30
#define	PSLEP	40
#define	PUSER	50

#define PRIMASK 0xff
#define PCATCH  0x100

/*
 * fundamental constants of the implementation--
 * cannot be changed easily
 */

#define	NBPW	sizeof(int)	/* number of bytes in an integer */
#define	BSIZE	512		/* size of secondary block (bytes) */
/* BSLOP can be 0 unless you have a TIU/Spider */
#define	BSLOP	2		/* In case some device needs bigger buffers */
#define	NINDIR	(BSIZE/sizeof(daddr_t))
#define	BMASK	0777		/* BSIZE-1 */
#define	BSHIFT	9		/* LOG2(BSIZE) */
#define	NMASK	0177		/* NINDIR-1 */
#define	NSHIFT	7		/* LOG2(NINDIR) */
#define	USIZE	16		/* size of user block (*64) */
#define	UBASE	0140000		/* abs. addr of user block */
#define	CMASK	0		/* default mask for file creation */
#define	NODEV	(dev_t)(-1)
#define	ROOTINO	((ino_t)2)	/* i number of all roots */
#define	SUPERB	1		/* block number of the super block */
#define	DIRSIZ(d)	16		/* max characters per directory */
#define	NICINOD	100		/* number of superblock inodes */
#define	NICFREE	50		/* number of superblock free blocks */
#define	INFSIZE	138		/* size of per-proc info for users */
#define	CBSIZE	14		/* number of chars in a clist block */
#define	CROUND	017		/* clist rounding: sizeof(int *) + CBSIZE - 1*/

#if 0 /* make this work in Punix on m68k */
/*
 * Some macros for units conversion
 */
/* Core clicks (64 bytes) to segments and vice versa */
#define	ctos(x)	((x+127)/128)
#define stoc(x) ((x)*128)

/* Core clicks (64 bytes) to disk blocks */
#define	ctod(x)	((x+7)>>3)

/* inumber to disk address */
#define	itod(x)	(daddr_t)((((unsigned)x+15)>>3))

/* inumber to disk offset */
#define	itoo(x)	(int)((x+15)&07)

/* clicks to bytes */
#define	ctob(x)	(x<<6)

/* bytes to clicks */
#define	btoc(x)	((((unsigned)x+63)>>6))

/* major part of a device */
#define	major(x)	(int)(((unsigned)x>>8))

/* minor part of a device */
#define	minor(x)	(int)(x&0377)

/* make a device number */
#define	makedev(x,y)	(dev_t)((x)<<8 | (y))

typedef	long		daddr_t;
typedef char *		caddr_t;
#endif

#define itod(x) (x<<16)
#define itoo(x) 0

/*
 * Machine-dependent bits and macros
 */
#define	UMODE	0x2000		/* usermode bits */
#define	USERMODE(ps)	(((ps) & UMODE)==0)

#if 0
#define	INTPRI	0340		/* Priority bits */
#define	BASEPRI(ps)	((ps & INTPRI) != 0)
#endif