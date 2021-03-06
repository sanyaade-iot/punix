/*
 * Punix, Puny Unix kernel
 * Copyright 2005 Chris Williams
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

#include <errno.h>
#include <inttypes.h>
#include <syscall.h>

#include "proc.h"
#include "sysent.h"
#include "punix.h"
#include "process.h"
#include "queue.h"
#include "inode.h"
#include "globals.h"

/* Processor Status word bit states */
#define PS_C	0x0001	/* carry */
#define PS_V	0x0002	/* overflow */
#define PS_Z	0x0004	/* zero */
#define PS_N	0x0008	/* negative */
#define PS_X	0x0010	/* extend */
#define PS_IPL	0x0700	/* interrupt priority level */
#define PS_SUP	0x2000	/* supervisor mode */
#define PS_T	0x8000	/* trace */

/*
 * system call function
 * upon return, carry indicates error
 */
/* callno is the system call number
 * ctx is the context of this syscall
 *  (used to set/clear carry and for vfork()/execve())
 */
STARTUP(uint32_t syscall(unsigned callno, struct context *ctx))
{
	extern const int nsysent;
	struct sysent *callp;
	uint32_t retval = 0;
	int sig;

	G.whereami = WHEREAMI_SYSCALL;
	
	/* for vfork(2) and execve(2) */
	P.p_syscall_ctx = ctx;
	//P.p_ustack = usp; /* is this actually necessary ? */
	
	/* get the system call entry */
	if (callno >= nsysent)
		callno = 0;
#if 0
	kprintf("SYS_%s (%d) ", sysname[callno], callno);
#endif

	callp = &sysent[callno];
	
#if USPARGS
#warning USPARGS might not be safe. See sys/proc.h for details.
	/* this is not safe for reasons listed in sys/proc.h */
	P.p_arg = &ctx->usp[1];
#else
	/* copy arguments from user stack to argument buffer in user struct */
	if (callp->sy_narg) /* &usp[1] below skips over return address */
		copyin(P.p_arg, &ctx->usp[1], callp->sy_narg*sizeof(int));
#endif
	
again:
	P.p_error = P.p_retval = 0;
	
	if (setjmp(P.p_sigjmp)) {
		/* we get here if a signal arrives during the system call */
		if (P.p_error == 0) {
			kprintf("warning: syscall interrupted by a signal but p_error=0!\n");
			P.p_error = EINTR;
		}
	} else {
		callp->sy_call();
	}
	
	if (P.p_error == ERESTART) {
		if (callp->sy_flags & SA_RESTART) {
			/*
			 * move the user PC backwards so it
			 * points to the trap instruction.
			 */
			retval = callno; // %d0 = callno
			ctx->pc -= 2; // XXX constant
		} else {
			P.p_error = EINTR;
		}
	}

	if (P.p_error == 0) {
		/* no error */
		ctx->sr &= ~PS_C; /* clear carry */
		retval = P.p_retval;
	} else if (P.p_error != ERESTART) {
		/* return the error */
		ctx->sr |= PS_C; /* set carry */
		retval = P.p_error;
	}
	
	/* another process has a higher priority than us */
	if (G.need_resched)
		swtch();
	
	/* XXX */
	if ((sig = issignal(&P)))
		postsig(sig);
	
	G.whereami = WHEREAMI_USER;
	return retval;
}

STARTUP(void sys_NONE(void))
{
	P.p_error = ENOSYS;
}
