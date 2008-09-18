/*
 * Punix, Puny Unix kernel
 * Copyright 2005, 2007 Chris Williams
 * 
 * $Id: sys_prot.c,v 1.2 2007/12/12 23:22:08 fredfoobar Exp $
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

#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>

#include "proc.h"
#include "file.h"
#include "punix.h"
#include "process.h"
/*
#include "uio.h"
*/
#include "inode.h"
#include "queue.h"
#include "globals.h"

STARTUP(void sys_getuid())
{
	P.p_retval = P.p_ruid;
}

STARTUP(void sys_geteuid())
{
	P.p_retval = P.p_uid;
}

STARTUP(void sys_getgid())
{
	P.p_retval = P.p_rgid;
}

STARTUP(void sys_getegid())
{
	P.p_retval = P.p_gid;
}

struct groupsa {
	int size;
	gid_t *list;
};

STARTUP(void sys_getgroups())
{
	struct groupsa *ap = (struct groupsa *)P.p_arg;
	gid_t *gp;
	int size;
	
	for (gp = &P.p_groups[0]; gp < &P.p_groups[NGROUPS]; ++gp)
		if (*gp == NOGROUP)
			break;
	
	/* return the actual number of group ID's */
	P.p_retval = size = gp - P.p_groups;
	
	/* if size is 0, return the actual number of groups IDs in the list */
	if (ap->size == 0)
		return;
	
	if (ap->size < size) {
		P.p_error = EINVAL;
		return;
	}
	
	P.p_error = copyout(ap->list, P.p_groups, size*sizeof(P.p_groups[0]));
}

/* XXX: check the following set functions for correctness and security */

#if 1
STARTUP(static int validuid(uid_t uid))
{
	if (uid > 0)
		return 1;
	
	P.p_error = EINVAL;
	return 0;
}
#else
#define validuid(uid) ((uid) > 0 ? 1 : (P.p_error = EINVAL, 0))
#endif

#if 0
STARTUP(static int validgid(gid_t gid))
{
	return validuid(gid);
}
#else
#define validgid(gid) validuid(gid)
#endif

STARTUP(void sys_setuid())
{
	struct a {
		uid_t uid;
	} *ap = (struct a *)P.p_arg;
	uid_t uid = ap->uid;
	
	if (!validuid(uid))
		return;
	
	if (P.p_uid == 0)
		P.p_uid =
		P.p_ruid =
		P.p_svuid = uid;
	else if (uid == P.p_ruid || uid == P.p_svuid)
		P.p_uid = uid;
	else
		P.p_error = EPERM;
}

STARTUP(void sys_setgid())
{
	struct a {
		gid_t gid;
	} *ap = (struct a *)P.p_arg;
	gid_t gid = ap->gid;
	
	if (!validgid(gid))
		return;
	
	if (P.p_uid == 0)
		P.p_gid =
		P.p_rgid =
		P.p_svgid = gid;
	else if (gid == P.p_rgid || gid == P.p_svgid)
		P.p_gid = gid;
	else
		P.p_error = EPERM;
}

STARTUP(int suser())
{
        if (P.p_uid == 0)
                return 1;

        P.p_error = EPERM;
        return 0;
}

/* XXX not in POSIX.1 */
STARTUP(void sys_setgroups())
{
	struct groupsa *ap = (struct groupsa *)P.p_arg;
	gid_t *gp;
	
	if (!suser())
		return;
	
	if (ap->size > sizeof(P.p_groups) / sizeof(P.p_groups[0])) {
		P.p_error = EINVAL;
		return;
	}
	
	P.p_error = copyin(P.p_groups, ap->list,
	 ap->size * sizeof(P.p_groups[0]));
	
	if (P.p_error)
		return;
	
	for (gp = &P.p_groups[ap->size]; gp < &P.p_groups[NGROUPS]; ++gp)
		*gp = NOGROUP;
}

STARTUP(void sys_seteuid())
{
	struct a {
		uid_t euid;
	} *ap = (struct a *)P.p_arg;
	uid_t euid = ap->euid;
	
	if (!validuid(euid))
		return;
	
	if (euid == P.p_ruid || euid == P.p_svuid || euid == P.p_uid || suser())
		P.p_uid = euid;
}

STARTUP(void sys_setegid())
{
	struct a {
		gid_t egid;
	} *ap = (struct a *)P.p_arg;
	uid_t egid = ap->egid;
	
	if (!validgid(egid))
		return;
	
	if (egid == P.p_rgid || egid == P.p_svgid || egid == P.p_gid || suser())
		P.p_gid = egid;
}

STARTUP(void sys_setreuid())
{
	struct a {
		uid_t ruid, euid;
	} *ap = (struct a *)P.p_arg;
	uid_t ruid = ap->ruid, euid = ap->euid;
	uid_t oldruid = P.p_ruid;
	
	if (ruid != -1 && validuid(ruid)
	&& (ruid == P.p_uid || ruid == P.p_ruid || suser()))
		P.p_ruid = ruid;
	
	if (euid != -1 && validuid(euid)
	&& (euid == oldruid || euid == P.p_svuid || euid == P.p_uid || suser()))
		P.p_uid = euid;
}

STARTUP(void sys_setregid())
{
	struct a {
		gid_t rgid, egid;
	} *ap = (struct a *)P.p_arg;
	gid_t rgid = ap->rgid, egid = ap->egid;
	gid_t oldrgid = P.p_rgid;
	
	if (rgid != -1 && validgid(rgid)
	&& (rgid == P.p_svgid || rgid == P.p_rgid || suser()))
		P.p_rgid = rgid;
	
	if (egid != -1 && validgid(egid)
	&& (egid == oldrgid || egid == P.p_svgid || egid == P.p_gid || suser()))
		P.p_gid = egid;
}
