#include <errno.h>

#include "punix.h"
#include "param.h"
#include "buf.h"
#include "dev.h"
#include "ioport.h"
#include "proc.h"
#include "queue.h"
#include "inode.h"
#include "globals.h"

/* one-shot routine on system startup */
STARTUP(void linkinit())
{
	qclear(&G.linkreadq);
	qclear(&G.linkwriteq);
}

STARTUP(static void rxon())  { LINK_CONTROL |=  LC_TRIGRX; }
STARTUP(static void rxoff()) { LINK_CONTROL &= ~LC_TRIGRX; }
STARTUP(static void txon())  { LINK_CONTROL |=  LC_TRIGTX; }
STARTUP(static void txoff()) { LINK_CONTROL &= ~LC_TRIGTX; }

STARTUP(void linkopen(dev_t dev, int rw))
{
	if (ioport) {
		P.p_error = EBUSY;
		return;
	}
	
	++ioport; /* block other uses of the IO port */
	
	qclear(&G.linkreadq);
	qclear(&G.linkwriteq);
	
	LINK_CONTROL = LC_AUTOSTART | LC_TRIGERR | LC_TRIGANY | LC_TRIGRX;
	/* XXX: anything more? */
}

STARTUP(static void flush())
{
	int x = spl5();
	/* wait until the write queue is empty */
	while (G.linkwriteq.q_count > 0) {
		G.linklowat = 0;
		slp(&G.linkwriteq);
	}
	splx(x);
}

STARTUP(void linkclose(dev_t dev, int flag))
{
	flush();
	qclear(&G.linkreadq); /* discard any unread data */
	ioport = 0; /* free the IO port for other uses */
}

STARTUP(void linkread(dev_t dev))
{
	/* read up to p_count bytes from the rx queue to p_base */
	int ch;
	size_t count = P.p_count;
	
	for (; P.p_count; --P.p_count) {
		while ((ch = getc(&G.linkreadq)) < 0) {
			if (count == P.p_count)
				slp(&G.linkreadq, PPIPE);
			else /* we got some data already, so just return */
				return;
		}
		*P.p_base++ = ch;
	}
}

STARTUP(void linkwrite(dev_t dev))
{
	int ch;
	
	for (; P.p_count; --P.p_count) {
		ch = *P.p_base++;
		while (putc(ch, &G.linkwriteq) < 0) {
			int x = spl5();
			G.linklowat = QSIZE - 32; /* XXX constant */
			slp(&G.linkwriteq, PPIPE);
			splx(x);
		}
	}
}

/*
 * FIXME: do rxon() to resume receiving when buffer has room to accept more
 * data and txon() to resume transmitting when buffer has more data.
 */
STARTUP(void linkintr())
{
	int status;
	int ch;
	
	(void)LINK_CONTROL; /* read to acknowledge interrupt */
	
	status = LINK_STATUS;
	
	if (status & LS_ACTIVITY) {
		/* do nothing */
		return;
	}
	
	if (status & LS_ERROR) {
		/* acknowledge the error */
		
		/* LC_AUTOSTART | LC_DIRECT | LC_TODISABLE */
		LINK_CONTROL = 0xe0;
		
		/* LC_AUTOSTART | LC_TRIGERR | LC_TRIGANY | LC_TRIGRX */
		LINK_CONTROL = 0x8d;
		
		return;
	}
	
	if (status & LS_RXBYTE) {
		ch = LINK_BUFFER;
		/* put the byte on the receive queue */
		putc(ch, &G.linkreadq);
		
		if (qisfull(&G.linkreadq))
			rxoff(); /* no room for the next byte */
		else if (G.linkreadq.q_count == 1) {
			spl3();
			wakeup(&G.linkreadq);
		}
	}
	
	if (status & LS_TXEMPTY) {
		/* get the next byte from the send queue and send it */
		
		if ((ch = getc(&G.linkwriteq)) < 0)
			txoff();
		else {
			LINK_BUFFER = ch;
			
			if (G.linkwriteq.q_count <= G.linklowat) {
				G.linklowat = -1;
				spl3(); /* let other ints occur while we do wakeup */
				wakeup(&G.linkwriteq);
			}
		}
	}
}

STARTUP(void linkioctl(dev_t dev, int cmd, void *cmarg, int flag))
{
}
