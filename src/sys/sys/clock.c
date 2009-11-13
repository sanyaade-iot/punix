/*
 * Punix, Puny Unix kernel
 * Copyright 2008 Christopher Williams
 * 
 * $Id$
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <limits.h>

#include "punix.h"
#include "param.h"
#include "buf.h"
#include "dev.h"
#include "proc.h"
#include "queue.h"
#include "inode.h"
#include "callout.h"
#include "globals.h"

STARTUP(long hzto(struct timespec *tv))
{
	long ticks;
	struct timespec diff;
	struct timespec rt;
	int x = splclock();
	
	getrealtime(&rt);
	timespecsub(tv, &rt, &diff);
	
	if (diff.tv_sec < 0)
		ticks = 0;
	if ((diff.tv_sec + 1) <= LONG_MAX / HZ)
		ticks = diff.tv_sec * HZ + diff.tv_nsec / TICK;
	else
		ticks = LONG_MAX;
	
	splx(x);
	return ticks;
}

/*
 * This arranges for func(arg) to be called in time/HZ seconds.
 * The callout array is sorted in order of times as a delta list.
 */
STARTUP(int timeout(void (*func)(void *), void *arg, long time))
{
	struct callout *c1, *c2;
	long t;
	int x;
	
	t = time;
	c1 = &G.callout[0];
	x = spl7();
	
	//kprintf("timeout: adding a timeout in %ld ticks\n", time);
	while (c1->c_func != NULL && c1->c_dtime <= t) {
		t -= c1->c_dtime;
		++c1;
	}
	
	if (c1 >= &G.callout[NCALL-1])
		return -1;
	
	c1->c_dtime -= t;
	c2 = c1;
	
	/* find the last callout entry */
	while (c2->c_func != NULL)
		++c2;
	c2[1].c_func = NULL;
	
	/* move entries upward to make room for this new entry */
	while (c2 >= c1) {
		c2[1] = c2[0];
		--c2;
	}
	
	c1->c_dtime = t;
	c1->c_func = func;
	c1->c_arg = arg;
	
	splx(x);
	return 0;
}

/*
 * remove a pending callout
 */
STARTUP(void untimeout(void (*func)(void *), void *arg))
{
	struct callout *cp;
	int x;
	x = spl7();
	for (cp = &G.callout[0]; cp < &G.callout[NCALL]; ++cp) {
		if (cp->c_func == func && cp->c_arg == arg) {
			if (cp < &G.callout[NCALL-1] && cp[1].c_func)
				cp[1].c_dtime += cp[0].c_dtime;
			while (cp < &G.callout[NCALL-1]) {
				*cp = *(cp+1);
				++cp;
			}
			G.callout[NCALL-1].c_func = NULL;
			break; /* remove only the first timeout */
		}
	}
	splx(x);
}
