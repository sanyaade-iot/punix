/* $Id: ktime.h,v 1.9 2008/04/19 18:41:15 fredfoobar Exp $ */

#ifndef _KTIME_H_
#define _KTIME_H_

#include <time.h>

/*
 * I think I've settled on using struct timespec for all internal time units.
 * It represents times with nanosecond resolution, so a tick (1/256 second) is
 * an integral number of nanoseconds (3,906,250 nanoseconds). It can also be
 * converted to struct timeval easily.
 */
#if 0
struct ktimeval {
	long tv_sec; /* seconds */
	int tv_tick; /* ticks */
};

struct kitimerval {
	struct ktimeval it_interval;
	struct ktimeval it_value;
};
#endif

void timespecadd(struct timespec *t1, struct timespec *t2);
void timespecsub(struct timespec *t1, struct timespec *t2);

#define timespecclear(tvp) (tvp)->tv_sec = (tvp)->tv_nsec = 0
#define timespecisset(tvp) ((tvp)->tv_sec || (tvp)->tv_nsec)
#define timespeccmp(tvp, uvp, cmp) \
        ((tvp)->tv_sec cmp (uvp)->tv_sec || \
         ((tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_nsec cmp (uvp)->tv_nsec))



#endif