#include <termios.h>

#include "flash.h"
#include "callout.h"
#include "queue.h"
#include "inode.h"
#include "tty.h"
#include "heap.h"
#include "buf.h"
#include "kbd.h"
#include "glyph.h"
#include "sched.h"

struct globals {
/* these must be first -- referenced from assembly code */
	struct timespec _walltime; /* must be first! see entry.s (Int_3) */
	struct timespec _realtime;
	long _timedelta;
	char exec_ram[60];
	int _ioport;
	int _updlock;
	struct proc *_current;
	int lowestpri;
	/* struct proc *freeproc; */
	struct proc *initproc;
	struct proc *proclist;
	struct list_head proc_list;
	int numrunning;
	long cumulrunning;
	struct file file[NFILE];
	unsigned long loadavg[3];
	struct timespec _uptime;
	long _loadavtime;
	
	//struct list_head runqueue;
	long prio_ratios[40];
	int need_resched;
	struct list_head runqueues[PRIO_LIMIT];
	volatile unsigned long ticks;
	
	unsigned char audiosamp; /* current samples */
	int audiosamples; /* number of samples within that byte */
	int audiolowat; /* low water level in audio queue */
	int audioplay; /* flag to indicate if audio should play */
	long long audiooptr;
	
	struct {
		int lowat, hiwat;
		struct queue readq, writeq;
		char control;
		int readoverflow;
	} link;
	
	struct queue audioq;
	
	/* seed for the pseudo-random number generator */
	unsigned long prngseed;
	
	dev_t rootdev, pipedev;
	struct inode *rootdir;
	
	char canonb[CANBSIZ];
	struct inode inode[NINODE];
	struct inode *inodelist;
	struct list_head inode_list;
	uid_t mpid;
	unsigned int pidchecked;
	struct callout callout[NCALL];
	int calloutlock;
	
	struct buf avbuflist; /* list of buf */
	struct list_head avbuf_list; /* list of available buf */
	int numbufs;
	/* struct buf buf[NBUF]; */
	
	struct flashblock *currentfblock;
	struct flash_cache_entry flash_cache[FLASH_CACHE_SIZE];
	
	int contrast;
	/* dev_vt static variables */
	struct {
		unsigned char xon;
		unsigned char nullop;
		int privflag;
		char intchars[2+1];
		char *intcharp;
		int params[16];
		unsigned char numparams;
		unsigned char cursorvisible;
		unsigned char tabstops[(60+7)/8];
		struct state const *vtstate;
		struct glyphset *glyphset, *charsets[2];
		unsigned char charset;
		unsigned char margintop, marginbottom;
		struct pos {
			int row, column;
		} pos;
#if 0
		struct row {
			struct cell {
				struct attrib {
					int bold:1;
					int underscore:1;
					int blink:1;
					int reverse:1;
				} attrib;
				int c;
			} cells[60];
		} screen[20];
#endif
		struct tty vt[1];
		
		char key_array[KEY_NBR_ROW];
		short key_mod, key_mod_sticky;
		short key_compose;
		unsigned char key_caps;
		unsigned char compose;
		unsigned char key_repeat; /* repeat enabled? */
		unsigned char key_repeat_delay;
		unsigned char key_repeat_start_delay;
		unsigned char key_repeat_counter;
		short key_previous;
		char gr; /* graphics rendition */
		int lock;
		int scroll_lock;
		int bell;
	} vt;
	
	int batt_level;
	
	int lbolt;
	
	/* temp/debugging variables */
	int whereami;
	int spin;
	struct timeval lasttime;
	struct rusage lastrusage;
	char charbuf[128];
	int charbufsize;
	int nextinode;
	/* end temp/debugging variables */
	
	int heapsize;
	struct heapentry heaplist[HEAPSIZE];
	char heap[0][HEAPBLOCKSIZE];
};

# if 0
extern struct globals G;
extern int ioport;
extern long walltime;

extern int updlock;
# else

#define G (*(struct globals *)0x5c00)
#define walltime G._walltime
#define realtime G._realtime
#define timedelta  G._timedelta
#define ioport   G._ioport
#define updlock  G._updlock
#define current  G._current
#define loadavtime G._loadavtime
#define uptime   G._uptime

# endif
