#include <string.h>
#include <errno.h>

#include "punix.h"
#include "heap.h"
#include "globals.h"

/*
 * These routines implement a memory allocator using an array of heap entries
 * to represent current allocations.
 */

/*
 * TODO: assign owners to allocations
 *
 * This can be done by using the pid field to store which module owns that
 * block. The owner will be a negative value since a positive value is a pid,
 * and 0 will indicate an unused block (currently -1 is used for this purpose).
 * The negative of an owner will be used as an index into a table of pointers
 * to functions which are used to request the module to free a memory block.
 * The function will take a pointer and will return a boolean value--whether
 * the block was freed or not. This will be used in allocentry when memory is
 * running low to try to free memory that is otherwise not needed (eg, caches).
 * One example owner is the block buffer cache; a block of memory may belong to
 * a buffer. If the buffer is locked, the module's function will return false
 * (0) right away. If the buffer is unlocked, it can be written out (if dirty),
 * freed, and the function will return true (non-0).
 *
 * If the function returns true, allocentry will either retry allocating memory,
 * or will again request a module to free some of its memory.
 *
 * It would also be a good idea to add a parameter to memalloc() whereby the
 * caller can indicate whether it is willing to wait for this process, or if it
 * would rather fail immediately.
 */

#define heapblock_to_offset(h) ((size_t)(h) * HEAPBLOCKSIZE)
#define offset_to_heapblock(o) (((o) + HEAPBLOCKSIZE - 1) / HEAPBLOCKSIZE)
#define heapentry_to_pointer(he) (G.heap.heap[(he)->start])

#if 1
void printheaplist()
{
	int i;
	kprintf("\n");
	for (i = 0; i < G.heap.heapsize; ++i) {
		const struct heapentry *h = &G.heap.heaplist[i];
		kprintf("%3d: %p %5lx/%5lx %-4d  ",
		        i,
			&G.heap.heap[h->start],
			HEAPBLOCKSIZE*(long)(h->end-h->start),
			i == G.heap.heapsize-1 ? 0L :
			 HEAPBLOCKSIZE*(long)(h[1].start-h[0].end),
			(int)h->pid);
	}
	kprintf("\n");
}
#endif

#define EACHENTRY(h) ((h) = &G.heap.heaplist[1]; (h) < &G.heap.heaplist[G.heap.heapsize]; ++(h))

#if 0
static void printfree()
{
	int x = 0;
	struct heapentry *hp;
	for EACHENTRY(hp) {
		x += hp->start - hp[-1].end;
	}
	kprintf("free: %ld bytes\n", (long)HEAPBLOCKSIZE * x);
}
#endif

#if 0
static size_t largest_unallocated_chunk_size()
{
	int size = 0;
	int prevend = 0;
	int s;
	struct heapentry *hp;
	for (hp = &G.heap.heaplist[1]; hp < &G.heap.heaplist[G.heap.heapsize]; ++hp) {
		s = hp->start - prevend;
		if (size < s)
			size = s;
		prevend = hp->end;
	}
	return (size_t)size * HEAPBLOCKSIZE;
}
#endif

void meminit()
{
	struct heapentry *hp = &G.heap.heaplist[0];
	/* insert two sentinal heap entries,
	 * one at the bottom and one at the top of the heap */
	hp->start = 0;
	hp->end = 0;
	hp->pid = 0;
	++hp;
	hp->start = ((void *)0x40000 - (void *)G.heap.heap[0]) / HEAPBLOCKSIZE;
	hp->end = hp->start;
	hp->pid = 0;
	G.heap.heapsize = 2;
	//printmemstats(NULL);
	
#if 0
	struct var {
		size_t size;
		char *name;
	} var[50];
	int numvars = 0;
#define ADDVAR(n) do { var[numvars].size = sizeof(G.n); var[numvars++].name = #n; } while (0)
	ADDVAR(exec_ram);
	ADDVAR(_realtime);
	ADDVAR(_runrun);
	ADDVAR(_istick);
	ADDVAR(_ioport);
	ADDVAR(_cputime);
	ADDVAR(_updlock);
	ADDVAR(_current);
	ADDVAR(lowestpri);
	ADDVAR(initproc);
	ADDVAR(proclist);
	ADDVAR(file);
	ADDVAR(loadavg);
	ADDVAR(audiosamp);
	ADDVAR(audiosamples);
	ADDVAR(audiolowat);
	ADDVAR(audioplay);
	ADDVAR(audiooptr);
	
	ADDVAR(linklowat);
	
	ADDVAR(linkreadq);
	ADDVAR(linkwriteq);
	ADDVAR(audioq);
	
	ADDVAR(prngseed);
	
	ADDVAR(rootdev);
	ADDVAR(pipedev);
	ADDVAR(rootdir);
	
	ADDVAR(canonb);
	ADDVAR(inode);
	ADDVAR(mpid);
	ADDVAR(pidchecked);
	ADDVAR(callout);
	
	ADDVAR(avbuflist);
	
	ADDVAR(currentfblock);
	ADDVAR(flash_cache);
	
	ADDVAR(contrast);
	ADDVAR(vt);
	
	ADDVAR(batt_level);
	
	ADDVAR(heap.heapsize);
	ADDVAR(heap.heaplist);
	ADDVAR(heap.heap);
	/* sort */
	int i, j;
	struct var t;
	for (i = 0; i < numvars - 1; ++i) {
		for (j = i; j >= 0; --j) {
			if (var[j].size >= var[j+1].size) break; /* in order */
			t = var[j];
			var[j] = var[j+1];
			var[j+1] = t;
		}
	}
	size_t totalsize = 0;
	for (i = 0; i < numvars; ++i)
		totalsize += var[i].size;
	kprintf("totalsize = %ld\n", totalsize);
	for (i = 0; i < numvars && 100 * var[i].size / totalsize >= 1; ++i)
		kprintf("%2ld.%02ld%% %-13.13s", 100 * var[i].size / totalsize, (10000 * var[i].size / totalsize) % 100, var[i].name);
	kputchar('\n');
	kprintf("%5ld largest unallocated chunk size\n", largest_unallocated_chunk_size());
	void *p;
	size_t s = (size_t)128;
	for (i = 0; i < 8000; ++i) {
		p = memalloc(&s, 0);
		if (p)
			memfree(p, 0);
	}
	printheaplist();
	/* abort(); */
#endif
}

/* insert an entry before existing entry hp, moving hp and other entries up */
static void insertentry(struct heapentry *hp, int start, int size, pid_t pid)
{
	assert(G.heap.heapsize < HEAPSIZE);
	assert(hp < &hp[G.heap.heapsize]);

	memmove(hp + 1, hp,
	        (void *)&G.heap.heaplist[G.heap.heapsize] - (void *)hp);
	++G.heap.heapsize;
	
	hp->start = start;
	hp->end = start + size;
	hp->pid = pid;
}

static struct heapentry *allocentry(int size, pid_t pid)
{
	struct heapentry *hp;
	
	if (size <= 0) return NULL;
	if (G.heap.heapsize >= HEAPSIZE) return NULL;
	
#define SIZETHRESHOLD 8192L
	if (size >= SIZETHRESHOLD / HEAPBLOCKSIZE) {
		/* allocate larger requests toward the bottom of memory */
		int prevend = 0;
		for (hp = &G.heap.heaplist[0];
		     hp < &G.heap.heaplist[G.heap.heapsize];
		     ++hp) {
			if (size <= hp->start - prevend) {
				insertentry(hp, prevend, size, pid);
				return hp;
			}
			prevend = hp->end;
		}
	}

	/* allocate smaller requests toward the top of memory */
	int nextstart = G.heap.heaplist[G.heap.heapsize-1].start;
	for (hp = &G.heap.heaplist[G.heap.heapsize-2];
	     hp >= &G.heap.heaplist[0];
	     --hp) {
		if (size <= (nextstart - hp->end)) {
			++hp;
			insertentry(hp, nextstart - size, size, pid);
			return hp;
		}
		nextstart = hp->start;
	}
	/* we couldn't find a slot big enough, so let's write out the oldest
	 * buffer in the avbuflist and try again if we could free that buffer */
	/* FIXME: first check to see if freeing buffers would possibly free up
	 * enough memory for this allocation to succeed. If the largest
	 * unallocated chunk size plus the size of all the buffers in the
	 * avbuflist (leaving at least MINBUF buffers) is less than the
	 * requested chunk size, then it would not do any good to flush any of
	 * the buffers */
	/* FIXME: maybe move this into a function in bio.c */
#if 0
	if (pid != 0 && G.avbuflist.b_avnext != &G.avbuflist) {
		struct buf *bp = G.avbuflist.b_avnext;
		if (buffree(bp)) goto loop;
	}
#endif
	return NULL;
}

/*
 * sizep is a pointer to the desired size. The actual size of the allocated
 * chunk is returned in the destination of sizep.
 * 
 * pid is the process id of the process that owns this memory chunk.
 */
void *memalloc(size_t *sizep, pid_t pid)
{
	size_t size;
	struct heapentry *hp;
	
	if (!sizep) {
		P.p_error = EINVAL;
		return NULL;
	}
	
	/*
	 * If *sizep is either 0 or is large enough that
	 * (*sizep + HEAPBLOCKSIZE - 1) wraps around, size will be 0,
	 * and the following test will catch this.
	 */
	size = (*sizep + HEAPBLOCKSIZE - 1) / HEAPBLOCKSIZE;
	if (size == 0)
		return NULL;
	//kprintf("memalloc(%5u, %d)\n", (int)size, pid);
	hp = allocentry(size, pid);
	if (!hp) {
		P.p_error = ENOMEM;
		return NULL;
	}
	*sizep = (size_t)HEAPBLOCKSIZE * size;
	return &G.heap.heap[hp->start];
}

struct heapentry *findentry(void *ptr)
{
	struct heapentry *hp;
	int start;
	int lower, middle, upper;
	off_t offset = ptr - (void *)G.heap.heap;
	if ((offset % HEAPBLOCKSIZE) != 0) {
		// pointer is not aligned on a heap block boundary
		return NULL;
	}
	start = offset / HEAPBLOCKSIZE;
	lower = 1;
	upper = G.heap.heapsize - 1;
	
	do {
		middle = (lower + upper) / 2;
		hp = &G.heap.heaplist[middle];
		if (start < hp->start) {
			upper = middle;
		} else if (start >= hp->end) {
			lower = middle + 1;
		} else if (start == hp->start) {
			return hp;
		} else {
			break;
		}
	} while (lower < upper);
	return NULL;
}

/* TODO: make sure this works properly! */
static void removeentry(struct heapentry *hp)
{
	memmove(hp, hp + 1,
	        (void *)&G.heap.heaplist[G.heap.heapsize] - (void *)hp);
	--G.heap.heapsize;
}

/* reallocate a previously-allocated block of memory */
/* the direction argument specifies which
 * end of the reallocated block changes (expand or shrink):
 * <0 : bottom (data at top stays at the same address)
 * =0 : block can move around freely (but data at the bottom stays at the base address of the new block). This is basically the same behavior as realloc(3)
 * >0 : top (data at bottom stays at the same address)
 * note: this only works at the granularity of the allocation size (eg, 16 bytes)
 * note2: if the direction can't be honored, memrealloc() returns NULL
 */
void *memrealloc(void *ptr, size_t *newsizep, int direction, pid_t pid)
{
	struct heapentry *hp, *newhp;
	size_t size, oldsize;
	void *newptr;

	if (!ptr)
		return memalloc(newsizep, pid);
	
	hp = findentry(ptr);
	if (!hp || (pid && pid != hp->pid)) {
		P.p_error = EFAULT;
		return NULL;
	}
	
	oldsize = hp->end - hp->start;
	ptr = &G.heap.heap[hp->start];
	
	size = (*newsizep + HEAPBLOCKSIZE - 1) / HEAPBLOCKSIZE;
	/* memrealloc() called with a size of 0 is the same as memfree() */
	if (size == 0) {
		removeentry(hp);
		return NULL;
	}
	
	/* easiest case: same size */
	if (size == oldsize) {
		goto done;
	}
	
	/* second easiest case: block shrinks */
	if (size < oldsize) {
		if (direction < 0) {
			hp->start = hp->end - size;
		} else {
			hp->end = hp->start + size;
		}
		goto done;
	}
	
	/* third easiest case: block expands */
	if (direction < 0) {
		if (size > hp[0].end - hp[-1].end) {
			return NULL;
		}
		hp->start = hp->end - size;
	} else if (direction > 0) {
		if (size > hp[1].start - hp[0].start) {
			return NULL;
		}
		hp->end = hp->start + size;
	} else {
		if (size <= hp[1].start - hp[0].start) {
			hp->end = hp->start + size;
		} else if (size <= hp[0].end - hp[-1].end) {
			/* move the block down in its existing slot */
			hp->start = hp->end - size;
			newptr = &G.heap.heap[hp->start];
			/* move the old block down */
			memmove(newptr, ptr,
			        (size_t)(hp->end - hp->start) * HEAPBLOCKSIZE);
		} else {
			/* find a new location */
			newhp = allocentry(size, hp->pid);
			if (!newhp)
				return NULL;
			newptr = &G.heap.heap[newhp->start];
			/* copy the data from ptr to newptr */
			memmove(newptr, ptr, (size_t)(hp->end - hp->start) *
			                              HEAPBLOCKSIZE);
			removeentry(hp);
		}
	}
		
done:
	*newsizep = (size_t)HEAPBLOCKSIZE * size;
	return &G.heap.heap[hp->start];
}

/* free all memory allocations for the given pid */
void memfreepid(pid_t pid)
{
	struct heapentry *hp;
	
	assert(pid > 0);

	for (hp = &G.heap.heaplist[0];
	     hp < &G.heap.heaplist[G.heap.heapsize];
	     ++hp) {
		if (pid == hp->pid) {
			removeentry(hp);
			--hp;
		}
	}
}

void memfree(void *ptr, pid_t pid)
{
	struct heapentry *hp;
	
	if (ptr == NULL) {
		return;
	}
	hp = findentry(ptr);
	if (!hp || (pid && pid != hp->pid)) {
		P.p_error = EFAULT;
		return;
	}
	removeentry(hp);
}

size_t heap_get_total()
{
	return (size_t)HEAPBLOCKSIZE *
	       (G.heap.heaplist[G.heap.heapsize-1].start -
	        G.heap.heaplist[0].start);
}

size_t heap_get_used()
{
	size_t s = 0;
	struct heapentry *hp;

	for (hp = &G.heap.heaplist[0];
	     hp < &G.heap.heaplist[G.heap.heapsize];
	     ++hp) {
		s += hp->end - hp->start;
	}
	return s * HEAPBLOCKSIZE;
}

size_t heap_get_free()
{
	return heap_get_total() - heap_get_used();
}

#if 0
void printmemstats(void *unused)
{
	(void)unused;
	kprintf("%lu total, ",
	        heap_get_total());
	kprintf("%lu used, ",
	        heap_get_used());
	kprintf("%lu free\n",
	        heap_get_free());
	timeout(printmemstats, NULL, HZ*5L);
}
#endif

void sys_kmalloc()
{
	struct a {
		size_t *sizep;
	} *ap = (struct a *)P.p_arg;
	void *p = NULL;
	
	P.p_retval = (unsigned long)p =
	  memalloc(ap->sizep, P.p_pid);
#if 0
	/* is it really necessary to zero out the memory? */
	if (p) {
		memset(p, 0, *ap->sizep);
	}
#endif
}

void sys_krealloc()
{
	struct a {
		void *ptr;
		size_t *sizep;
		int direction;
	} *ap = (struct a *)P.p_arg;
	void *p = NULL;
	
	P.p_retval = (unsigned long)p =
	  memrealloc(ap->ptr, ap->sizep, ap->direction, P.p_pid);
}

void sys_kfree()
{
	struct a {
		void *ptr;
	} *ap = (struct a *)P.p_arg;
	
	if (ap->ptr)
		memfree(ap->ptr, P.p_pid);
}
