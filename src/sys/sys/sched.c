/* new Earliest Effective Virtual Deadline First scheduler,
 * based on BFS by Con Kolivas */

#include <errno.h>
#include <sched.h>

#include "list.h"
#include "punix.h"
#include "globals.h"
#include "sched.h"
#include "proc.h"
#include "ticks.h"

#if 0
#define NICE_TO_PRIO(nice) ((nice) + 20)
#define PRIO_TO_NICE(prio) ((prio) - 20)
#define PROC_NICE(procp) PRIO_TO_NICE((procp)->p_nice)
#endif

#define MS_TO_TICKS(t) (((long)(t) * HZ + 500) / 1000)

/* RR_INTERVAL is the round-robin interval in milliseconds */
#define RR_INTERVAL (20L) //(1000L*16/HZ)
/* TIME_SLICE is the same as RR_INTERVAL but measured in ticks */
#define TIME_SLICE MS_TO_TICKS(RR_INTERVAL)

#define rt_prio(prio)           ((prio) < MAX_RT_PRIO)
#define rt_proc(p)              rt_prio((p)->p_prio)
#define iso_proc(p)             ((p)->p_sched_policy == SCHED_ISO)
#define idle_proc(p)            ((p)->p_sched_policy == SCHED_IDLE)
#define is_rt_policy(policy)    ((policy) == SCHED_FIFO || \
                                 (policy) == SCHED_RR)
#define has_rt_policy(p)        (is_rt_policy((p)->p_sched_policy))
#define ISO_PERIOD              ((5 * HZ) + 1)

static int prio_deadline_offset(int prio)
{
	return ((G.prio_ratios[prio]*TIME_SLICE + F_HALF) / F_ONE) ? : 1;
}

static int longest_deadline_offset(void)
{
	return prio_deadline_offset(39);
}

void sched_init(void)
{
	int i;
	G.prio_ratios[0] = F_ONE;
	for (i = 1; i < PRIO_RANGE; ++i)
		G.prio_ratios[i] = (G.prio_ratios[i-1] * 11 + 5) / 10;
	for (i = 0; i < PRIO_LIMIT; ++i)
		INIT_LIST_HEAD(&G.runqueues[i]);
	/* TODO: do whatever else is needed */
}

/* select the next process to run. */
static struct proc *earliest_deadline_proc(void)
{
	unsigned long dl, earliest_deadline = 0;
	struct proc *procp, *earliest_procp = NULL;
	int idx = NORMAL_PRIO;
	struct list_head *queue;
	
	do {
		queue = &G.runqueues[idx];
		list_for_each_entry(procp, queue, p_runlist) {
			//kprintf(".");
			if (idx < MAX_RT_PRIO) {
				earliest_procp = procp;
				goto out;
			}
			
			dl = procp->p_deadline;
#if 0
			// select a process immediately if its deadline is past
			if (time_before(dl, G.ticks)) {
				earliest_procp = procp;
				goto out;
			}
#endif
			if (earliest_procp == NULL ||
			    time_before(dl, earliest_deadline)) {
				earliest_procp = procp;
				earliest_deadline = dl;
			}
		}
	} while (!earliest_procp && ++idx < PRIO_LIMIT);
	
out:
	if (!earliest_procp) {
		++*(long *)(0x4c00+0xf00-30*2);
	}
	return earliest_procp;
}

#if 1
STARTUP(void swtch())
{
	struct proc *p;
	masklock m;
	mask(&G.calloutlock);
	m = G.calloutlock;
	
	//kprintf("swtch() ");
	
	/* XXX: this shows the number of times this function has been called.
 	 * It draws in the bottom-left corner of the screen.
	 */
#if 1
	++*(long *)(0x4c00+0xf00-30);
#endif
	
	if (current && current->p_time_slice <= 0) {
		/* process used its whole time slice */
		current->p_first_time_slice = 0;
		current->p_time_slice += TIME_SLICE;
		current->p_deadline = G.ticks + prio_deadline_offset(current->p_nice);
		if (idle_proc(current))
			current->p_deadline += longest_deadline_offset();
	}
	
	while (!(p = earliest_deadline_proc())) {
		G.cpubusy = 0;
		showstatus();
		struct proc *pp = current;
		current = NULL; /* don't bill any process if they're all asleep */
		setmask(&G.calloutlock, 0);
		cpuidle(INT_ALL);
		setmask(&G.calloutlock, m);
		current = pp;
		//istick = 1; /* the next process will start on a tick */
	}
	G.cpubusy = 1;
	showstatus();
	
	G.need_resched = 0;
	
	if (p == current)
		goto out;
	
	if (!P.p_fpsaved) {
		/* savefp(&P.p_fps); */
		P.p_fpsaved = 1;
	}
	
	if (csave(&P.p_ctx))
		goto out; /* we get here via crestore */
	
	current = p;
	
	// inhibit soft interrupts and clear soft interrupt mask
	// this is needed so the mask is cleared for vfork
	spl1();
	setmask(&G.calloutlock, 0);

	crestore(&P.p_ctx); /* jump to the csave() for this context */

out:
	setmask(&G.calloutlock, m);
	unmask(&G.calloutlock);
}
#endif

/* called from timer at HZ times per second */
void sched_tick(void)
{
	if (!current) return;
	if (--current->p_time_slice <= 0) {
		++G.need_resched;
	}
}

#if 1
/* set the state of the process to 'state' */
static void set_state(struct proc *p, const int state)
{
	if (state == p->p_state) return;
	mask(&G.calloutlock);
	if (state == P_RUNNING) {
		++G.numrunning;
		/* preempt the current process */
		if (!current ||
		    time_before(p->p_deadline, current->p_deadline)) {
			++G.need_resched;
			//istick = 0;
		}
		list_add_tail(&p->p_runlist, &G.runqueues[p->p_prio]);
	} else {
		if (p->p_state == P_RUNNING) {
			--G.numrunning;
			list_del(&p->p_runlist);
		} else {
			//kprintf("set_state from not P_RUNNING to another not P_RUNNING\n");
		}
	}
	p->p_state = state;
	unmask(&G.calloutlock);
	/* TODO: anything else? */
}
#endif

/* put the given proc on the run queue, possibly preempting the current proc */
void sched_run(struct proc *p)
{
	mask(&G.calloutlock);
	//kprintf("sched_run()\n");
#if 0
	if (p->p_state == P_RUNNING) {
		kprintf("pid=%d cmd=%s\n", p->p_pid, p->p_name);
		panic("trying to run an already running process");
	}
#endif
	if (p->p_state == P_ZOMBIE)
		panic("trying to run a zombie");
	set_state(p, P_RUNNING);
	/* TODO: anything else? */
	unmask(&G.calloutlock);
}

/* put the given process to sleep and take it off the run queue */
void sched_sleep(struct proc *p)
{
	//kprintf("sched_sleep(%08lx)\n", p);
	set_state(p, P_SLEEPING);
}

/* stop the given process and take it off the run queue */
void sched_stop(struct proc *p)
{
	//kprintf("sched_stop(%08lx)\n", p);
	set_state(p, P_STOPPED);
}

void sched_fork(struct proc *childp)
{
	//kprintf("sched_fork(%08lx)\n", childp);
	/*
	 * give the child half of our time_slice
	 * set first_time_slice in the child
	 */
	mask(&G.calloutlock);
	int half = current->p_time_slice / 2;
	childp->p_time_slice = half;
	current->p_time_slice -= half;
	childp->p_first_time_slice = 1;
	//current->p_deadline = G.ticks + prio_deadline_offset(current->p_nice);
	set_state(childp, P_RUNNING);
	unmask(&G.calloutlock);
}

void sched_exec(void)
{
	//kprintf("sched_exec()\n");
}

void sched_exit(struct proc *p)
{
	mask(&G.calloutlock);
	//kprintf("sched_exit(%08lx)\n", p);
	if (p->p_first_time_slice && p->p_pptr) {
		p->p_pptr->p_time_slice += p->p_time_slice;
	}
	set_state(p, P_ZOMBIE);
	unmask(&G.calloutlock);
}

#if 0
void sched_set_scheduler(struct proc *procp, int policy)
{
}

int sched_get_scheduler(struct proc *procp)
{
}
#endif

int sched_get_nice(const struct proc *p)
{
	return p->p_nice;
}

void sched_set_nice(struct proc *p, int newnice)
{
	int oldnice = p->p_nice;
	if (newnice == oldnice) return;
	p->p_nice = newnice;
	/* adjust the deadline for this process */
	p->p_deadline += prio_deadline_offset(newnice) -
	                 prio_deadline_offset(oldnice);
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	int oldscheduler;
	struct proc *p;
	p = pfind(pid);
	if (!p)
		return -1;
	
	/* FIXME: check that current proc has permission to set the given proc's
	 * priority to the given sched_param */
	
	P.p_error = EINVAL;
	return -1;
}

int sched_getscheduler(pid_t pid)
{
	struct proc *p;
	p = pfind(pid);
	if (!p) {
		return -1;
	}
	
	/* FIXME: check that current proc has permission to get the given proc's
	 * priority */
	return p->p_sched_policy;
}

int sched_yield(void)
{
	current->p_time_slice = 0;
	swtch();
	return 0;
}
