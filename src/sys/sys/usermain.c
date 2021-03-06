/*
 * Punix, Puny Unix kernel
 * Copyright 2009 Christopher Williams
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

#define _BSD_SOURCE

//#include <stdio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sound.h>
#include <sys/utsname.h>
#include <setjmp.h>
#include <sys/sysctl.h>
//#include <sys/ioctl.h>

#define _exit _Exit

/*
 * XXX: The following includes should not be needed in a real user progam.
 * They are included only due to the lack of global variables and for the
 * reference to G.batt_level
 */
#include "punix.h"
#include "globals.h"

int adjtime(const struct timeval *delta, struct timeval *olddelta);

/* simple implementations of some C standard library functions */
void *kmalloc(size_t *size);
void *krealloc(void *ptr, size_t *size, int direction);
void kfree(void *ptr);

void poweroff();


void perror(const char *s)
{
	int e = errno;
	if (s && *s)
		fprintf(stderr, "%s: ", s);
	fprintf(stderr, "%s\n", strerror(e));
}

void *malloc(size_t size)
{
	if (!size) return NULL;
	return kmalloc(&size);
}

void *calloc(size_t nelem, size_t elsize)
{
	void *ptr;
	size_t size;
	if (LONG_MAX / nelem < elsize)
		return 0;
	size = nelem * elsize;
	ptr = malloc(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void free(void *ptr)
{
	kfree(ptr);
}

void *realloc(void *ptr, size_t size)
{
	if (!size) {
		free(ptr);
		return NULL;
	}
	return krealloc(ptr, &size, 0);
}

time_t time(time_t *tp)
{
	struct timeval tv;
	time_t t;
	gettimeofday(&tv, NULL);
	t = tv.tv_sec;
	if (tp) *tp = t;
	return t;
}


#define EPOCHYEAR 1970
#define SECS_DAY 86400
#define YEAR0 1900
#define LEAPYEAR(year) ( (((year) % 4) == 0) && (((year) % 100) || ((year) % 400) == 0) )
#define YEARSIZE(year) (LEAPYEAR(year) ? 366 : 365)
static const int _ytab[2][12] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
};
/* following implementation from Minix? */
struct tm *gmtime_r(const time_t *tp, struct tm *tmp)
{
	time_t t = *tp;
	unsigned long dayclock, dayno;
	int year = EPOCHYEAR;

	dayclock = (unsigned long)t % SECS_DAY;
	dayno = (unsigned long)t / SECS_DAY;

	tmp->tm_sec = dayclock % 60;
	tmp->tm_min = (dayclock % 3600) / 60;
	tmp->tm_hour = dayclock / 3600;
	tmp->tm_wday = (dayno + 4) % 7;       /* day 0 was a thursday */
	while (dayno >= YEARSIZE(year)) {
		dayno -= YEARSIZE(year);
		++year;
	}
	tmp->tm_year = year - YEAR0;
	tmp->tm_yday = dayno;
	tmp->tm_mon = 0;
	while (dayno >= _ytab[LEAPYEAR(year)][tmp->tm_mon]) {
		dayno -= _ytab[LEAPYEAR(year)][tmp->tm_mon];
		tmp->tm_mon++;
	}
	tmp->tm_mday = dayno + 1;
	tmp->tm_isdst = 0;

	return tmp;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	time_t t = *timep;
	t -= 7 * 3600; /* XXX: works for me ;) */
	return gmtime_r(&t, result);
}

#if 0
       char *asctime(const struct tm *tm);
       char *asctime_r(const struct tm *tm, char *buf);

       char *ctime(const time_t *timep);
       char *ctime_r(const time_t *timep, char *buf);

       struct tm *gmtime(const time_t *timep);
       struct tm *gmtime_r(const time_t *timep, struct tm *result);

       struct tm *localtime(const time_t *timep);
       struct tm *localtime_r(const time_t *timep, struct tm *result);

       time_t mktime(struct tm *tm);
#endif

static const char _wtab[7][3] = {
	"Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static const char _mtab[12][3] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
char *asctime_r(const struct tm *tm, char *buf)
{
	sprintf(buf, "%3.3s %3.3s %2d %02d:%02d:%02d %4.4d\n",
	        _wtab[tm->tm_wday], _mtab[tm->tm_mon], tm->tm_mday,
	        tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
	return buf;
}

unsigned alarm(unsigned seconds)
{
	struct itimerval it, oldit;
	it.it_value.tv_sec = seconds;
	it.it_value.tv_usec =
	 it.it_interval.tv_sec =
	 it.it_interval.tv_usec = 0;

	setitimer(ITIMER_REAL, &it, &oldit);
#if 0
	/*
	 * We can't return 0 if there is any time remaining from a previous
	 * alarm() request. If there's at least a second remaining, just round
	 * the time to the nearest second.
	 */
	if ((!oldit.it_value.tv_sec && oldit.it_value.tv_usec) ||
	    oldit.it_value.tv_usec >= 500000L)
		++oldit.it_value.tv_sec;
#else
	/* round the remaining time up to a second */
	if (oldit.it_value.tv_usec)
		++oldit.it_value.tv_sec;
#endif
	return oldit.it_value.tv_sec;
}

static clock_t timeval_to_clock_t(struct timeval *tv)
{
	return tv->tv_sec * CLOCKS_PER_SEC +
	       tv->tv_usec * CLOCKS_PER_SEC / 1000000L;
}

static void clock_t_to_timeval(clock_t c, struct timeval *tv)
{
	tv->tv_sec = c / CLOCKS_PER_SEC;
	tv->tv_usec = (c % CLOCKS_PER_SEC) * 1000000L / CLOCKS_PER_SEC;
}

clock_t times(struct tms *buf)
{
	struct rusage self, children;
	struct timeval tod;
	gettimeofday(&tod, NULL);
	getrusage(RUSAGE_SELF, &self);
	getrusage(RUSAGE_CHILDREN, &children);
	buf->tms_utime = timeval_to_clock_t(&self.ru_utime);
	buf->tms_stime = timeval_to_clock_t(&self.ru_stime);
	buf->tms_cutime = timeval_to_clock_t(&children.ru_utime);
	buf->tms_cstime = timeval_to_clock_t(&children.ru_stime);
	return timeval_to_clock_t(&tod);
}

int sysctl(int *name, unsigned namelen, void *oldp, size_t *oldlenp,
           void *newp, size_t newlen);
int getloadavg32(long la[], int nelem)
{
	static int loadmib[] = { CTL_VM, VM_LOADAVG };
	static unsigned loadmiblen = sizeof(loadmib) / sizeof(*loadmib);
	size_t lalen = nelem * sizeof(long);

	int ret = sysctl(loadmib, loadmiblen, la, &lalen, NULL, 0L);
	if (ret)
		return -1;
	return lalen / sizeof(long);
}

#define ESC "\x1b"
/* erase from cursor to end of line (inclusive) */
static void cleareol()
{
	printf(ESC "[K");
}

static void clear()
{
	printf(ESC "[H" ESC "[J");
}

void userpause()
{
	ssize_t n;
	char buf[60];
	printf("Type ctrl-d to continue...\n");
	do
		n = read(0, buf, sizeof(buf));
	while (n != 0);
	putchar('\n');
}

static void sigalrm()
{
	printf("sigalrm\n");
}

int busyloop_main(int argc, char *argv[], char *envp[])
{
	for (;;) {
		nop();
		nop();
		nop();
		nop();
	}
	return 0;
}

static void testnice(int argc, char *argv[], char *envp[])
{
	int i;
	int pid;
	for (i = 0; i < 5; ++i) {
		pid = vfork();
		if (pid < 0) {
			fprintf(stderr, "testnice: vfork() failed\n");
			return;
		} else if (pid == 0) {
			static char *const argv[] = { "busyloop", NULL };
			int n = nice(i*5);
			fprintf(stderr, "nice %d\n", n);
			execve(argv[0], argv, envp);
			perror("execve");
			_Exit(1);
		}
	}
}

#include <stdlib.h>

long int strtol(const char *nptr, char **endptr, int base)
{
	static const char digits[36+1] = "0123456789abcdefghijklmnopqrstuvwxyz";
	unsigned long int result = 0;
	int sign = 0;

	/* first skip whitespace */
	while (isspace(*nptr))
		++nptr;
	
	/* get optional sign character */
	switch (*nptr) {
	case '-': sign = 1;
	case '+': ++nptr;
	}

	/* get optional leading 0x or 0 characters */
	if ((base == 0 || base == 16) && nptr[0] == '0' && tolower(nptr[1]) == 'x') {
		base = 16;
		nptr += 2;
	} else if ((base == 0 || base == 8) && nptr[0] == '0') {
		base = 8;
		nptr += 1;
	} else if (base == 0) {
		// default base
		base = 10;
	}

	unsigned long threshold = ((unsigned long)LONG_MAX + base - 1) / base;
#if 0
	unsigned long max = sign ? -(unsigned long)LONG_MIN : LONG_MAX;
#else
	unsigned long max = sign ? 0x80000000UL : 0x7fffffffUL;
#endif
	/* now get the digits! */
	while (*nptr) {
		const char *c = strchr(digits, tolower(*nptr));
		if (!c) break; // invalid character
		int digit = c - digits;
		if (digit >= base) break; // digit out of range
		if (result >= threshold) {
			/* multiplying by base will overflow result */
			result = max;
		} else {
			result = result * base + digit;
		}
		if (result > max) {
			/* result is overflowed */
			result = max;
		}
		++nptr;
	}
	if (endptr) {
		*endptr = (char *)nptr;
	}
	return sign ? -result : result;
}

typedef unsigned long softfloat;
softfloat fadd(softfloat, softfloat);

static void printsng(softfloat f)
{
	int sign = (f & 0x80000000L) != 0;
	int exp = ((f >> 23) & 0xff) - 127;
	long man = (f & 0x007fffffL);

	const char *signstr = sign ? "-" : "+";
	if (exp == 128) {
		if (man) {
			printf("%s%snan", signstr, man & 0x00400000L ? "" : "s");
		} else {
			printf("%sinf", signstr);
		}
		return;
	} else if (exp == -127) {
		if (man) {
			++exp;
			while (!(man & 0x00800000L)) {
				man <<= 1;
				--exp;
			}
		} else {
			exp = 0;
			printf("%s0", signstr);
			return;
		}
	} else {
		//man |= 0x00800000L;
	}
	man &= 0x7fffffL;
	man <<= 1;
	printf("%s0x1.%06lxp%d", signstr, man, exp);
}

union longlong {
	long long ll;
	struct int64 {
		unsigned long high, low;
	};
};

unsigned long long sl64(unsigned long long, unsigned);
unsigned long long sr64(unsigned long long, unsigned);

static void testsr64(int argc, char *argv[], char *envp[])
{
	long long x = 0x0102030405060708ULL;
	int i, j;
	int p = 0;
	for (i = 0; i < 4; ++i) {
		for (j = i; j < 68; j += 4) {
			union longlong y; y.ll = sr64(x, j);
			printf("%2d: %08lx %08lx\n", j, y.high, y.low);
			if (++p == 17) {
				userpause();
				p = 0;
			}
		}
	}
	if (p)
		userpause();
}

struct floattest {
	softfloat a, b, c;
};

static int testfloats(struct floattest test, int print)
{
	int correct;
	softfloat c = fadd(test.a, test.b);
	correct = c == test.c;
	//printf("0x%08lx + 0x%08lx = 0x%08lx\n", a, b, c);
	if (print) {
		if (correct) {
			printf("PASS\n");
		} else {
			printf("FAIL: expected: ");
			printsng(test.c);
			printf(" actual: ");
			printsng(c);
			putchar('\n');
		}
		//printsng(test.a); printf(" + "); printsng(test.b); printf(" != "); printsng(c);
		//printf(" (%s)\n", correct ? "correct" : "INCORRECT");
	}
	return correct;
}

static void testfpu(int argc, char *argv[], char *envp[])
{
	static const struct floattest tests[] = {
		/* x - x = +0 */
		{0x00000000, 0x00000000, 0x00000000}, /* +0.000000 + +0.000000 = +0.000000 */
		{0x80000000, 0x00000000, 0x00000000}, /* -0.000000 + +0.000000 = +0.000000 */
		{0x00000000, 0x80000000, 0x00000000}, /* +0.000000 + -0.000000 = +0.000000 */
		{0x80000000, 0x80000000, 0x80000000}, /* -0.000000 + +0.000000 = -0.000000 */
		{0x3f800000, 0xbf800000, 0x00000000}, /* 1.000000 + -1.000000  = +0.000000 */
		{0xbf800000, 0x3f800000, 0x00000000}, /* -1.000000 + 1.000000  = +0.000000 */

		{0x7f800000, 0x3f800000, 0x7f800000}, /* inf + 1.000000 = inf */
		{0x7f800000, 0xff800000, 0x7fc00000}, /* inf + -inf = nan */
		{0xff800000, 0x7f800000, 0x7fc00000}, /* -inf + inf = nan */
		{0x7fc04200, 0x3f800000, 0x7fc04200}, /* nan + 1.000000 = nan */
		{0x3f800000, 0x7fc00000, 0x7fc00000}, /* 1.000000 + nan = nan */
		{0x40000000, 0xbf800000, 0x3f800000}, /* 2.000000 + -1.000000 = 1.000000 */


		{0x428b6148, 0xbf800000, 0x42896148}, /* 69.690002 + -1.000000 = 68.690002 */
		{0x3f800000, 0xc28b6148, 0xc2896148}, /* 1.000000 + -69.690002 = -68.690002 */
		{0xc28b6148, 0xbf800000, 0xc28d6148}, /* -69.690002 + -1.000000 = -70.690002 */
		{0xbf800000, 0xc28b6148, 0xc28d6148}, /* -1.000000 + -69.690002 = -70.690002 */

	};
	int i;
	int pass = 0;
	int print = 1;
	if (argc > 1)
		print = atoi(argv[1]);
	for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
		if (print) {
			printf("test %2d: ", i);
		}
		pass += testfloats(tests[i], print);
		//userpause();
		//printf(ESC "[A" ESC "[A"); // cursor up
	}
	if (print) {
		printf("pass: %d/%d\n", pass, i);
		//fputest();
		userpause();
	}
}

static void testbase(const char *str, int base)
{
	char *endptr;
	long x = strtol(str, &endptr, base);
	printf("base %2d: %ld (end=%ld)\n", base, x, endptr-str);
}

static void teststrtol(int argc, char *argv[], char *envp[])
{
	static const char *tests[] = {
#if 0
		" 	\n1234567", " 	\n+1234567", " 	\n-1234567",
		" 	\n0x12345", " 	\n+0x12345", " 	\n-0x12345",
		" 	\n0123456", " 	\n+0123456", " 	\n-0123456",
		" 	\n0101101", " 	\n+0101101", " 	\n-0101101",
		"123k5", "34565243523452345", "", "09", "0xg",
#endif
		"2147483646",
		"2147483647",
		"2147483648",
		"-2147483647",
		"-2147483648",
		"-2147483649",
	};
	int i;
	for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
		printf("'%s' (length %ld):\n", tests[i], strlen(tests[i]));
		testbase(tests[i], 0);
		testbase(tests[i], 2);
		testbase(tests[i], 8);
		testbase(tests[i], 10);
		testbase(tests[i], 16);
		userpause();
	}
}

static void testclock(int argc, char *argv[], char *envp[])
{
	printf("system clock:  %ld\n", realtime.tv_sec);
	printf("seconds clock: %ld\n", G.seconds);
	userpause();
}

#define RANDBUFSIZE 100
static void testrandom(int argc, char *argv[], char *envp[])
{
	int i;
	int randomfd;
	int *randombuf;
	randombuf = malloc(RANDBUFSIZE*sizeof(int));
	if (!randombuf) {
		perror("testrandom");
		return;
	}
	
	randomfd = open("/dev/random", O_RDWR);
	printf("randomfd = %d\n", randomfd);
	
	if (randomfd >= 0) {
		read(randomfd, randombuf, RANDBUFSIZE*sizeof(int));
		for (i = 0; i < 100; ++i) {
			printf("%5u ", randombuf[i]);
		}
		printf("\n");
		close(randomfd);
	} else {
		perror("testrandom: /dev/random");
	}

	free(randombuf);
	
	userpause();
}

static const char onebit[] = {
#include "onebit.hex"
};
#define AUDIOBUFSIZE 4096
static void testaudio(int argc, char *argv[], char *envp[])
{
	int i;
	int audiofd;
	long *audioleft = NULL;
	long *audioright = NULL;
	long *audiocenter = NULL;
	
	audiofd = open("/dev/audio", O_RDWR);
	printf("audiofd = %d\n", audiofd);
	if (audiofd < 0) {
		printf("could not open audio device!\n");
		goto out;
	}

#if 0
	audioleft = malloc(AUDIOBUFSIZE*sizeof(long));
	audioright = malloc(AUDIOBUFSIZE*sizeof(long));
	audiocenter = malloc(AUDIOBUFSIZE*sizeof(long));
	if (!audioleft || !audioright || !audiocenter) {
		printf("could not allocate audio buffers!\n");
		goto free;
	}
	
	for (i = 0; i < AUDIOBUFSIZE; ++i) {
		audioleft[i] = 0x00aaaa00;
		audioright[i] = 0x00555500;
		audiocenter[i] = 0x00ffff00;
	}
	
	printf("playing left...\n");
	write(audiofd, audioleft, AUDIOBUFSIZE * sizeof(long));
	ioctl(audiofd, SNDCTL_DSP_SYNC);
	
	printf("playing right...\n");
	write(audiofd, audioright, AUDIOBUFSIZE * sizeof(long));
	ioctl(audiofd, SNDCTL_DSP_SYNC);
	
	printf("playing both...\n");
	write(audiofd, audiocenter, AUDIOBUFSIZE * sizeof(long));
#else
	write(audiofd, onebit, sizeof(onebit));
#endif
#if 0
	ioctl(audiofd, SNDCTL_DSP_SYNC); /* close() automatically sync's */
#endif
	
	printf("closing audio...\n");
	close(audiofd);
	
free:
	free(audioleft);
	free(audioright);
	free(audiocenter);
out:
	userpause();
}

static const char varpkt1[] = {
	0x88, 0x56, 0x00, 0x00, /* ACK */
	0x88, 0x09, 0x00, 0x00, /* CTS */
};
static const char varpkt2[] = {
	0x88, 0x56, 0x00, 0x00, /* ACK */
};

static const char rdypkt[] = {
	0x88, 0x92, 0x00, 0x00, /* RDY */
};

struct pkthead {
	unsigned char machid;
	unsigned char commid;
	unsigned int length;
};

static const struct machinfo {
	int id;
	char *desc;
} machinfo[] = {
	{ 0x08, "PC -> 89/92+/V200" },
	{ 0x88, "92+/V200 -> PC or 92+/V200 -> 92+/V200" },
	{ 0x98, "89 (Titanium) -> PC or 89 (Titanium) to 89 (Titanium)" },
	{ -1, "" },
};

static const struct comminfo {
	int id;
	char desc[5];
	int hasdata;
} comminfo[] = {
	{ 0x06, "VAR",  1 },
	{ 0x09, "CTS",  0 },
	{ 0x15, "DATA", 1 },
	{ 0x2d, "VER",  0 },
	{ 0x36, "MEM",  1 },
	{ 0x56, "ACK",  0 },
	{ 0x5a, "ERR",  0 },
	{ 0x68, "RDY",  0 },
	{ 0x6d, "SCR",  0 },
	{ 0x78, "CONT", 0 },
	{ 0x87, "CMD",  0 },
	{ 0x88, "DEL",  1 },
	{ 0x92, "EOT",  0 },
	{ 0xa2, "REQ",  1 },
	{ 0xc9, "RTS",  1 },
	{ -1, "", 0 }
};
#define PKT_COMM_VAR 0x06
#define PKT_COMM_CTS 0x09
#define PKT_COMM_DATA 0x15
#define PKT_COMM_XDP PKT_COMM_DATA
#define PKT_COMM_VER 0x2d
#define PKT_COMM_MEM 0x36
#define PKT_COMM_ACK 0x56
#define PKT_COMM_OK PKT_COMM_ACK
#define PKT_COMM_ERR 0x5a
#define PKT_COMM_RDY 0x68
#define PKT_COMM_SCR 0x6d
#define PKT_COMM_CONT 0x78
#define PKT_COMM_CMD 0x87
#define PKT_COMM_DEL 0x88
#define PKT_COMM_EOT 0x92
#define PKT_COMM_DONE PKT_COMM_EOT
#define PKT_COMM_REQ 0xa2
#define PKT_COMM_RTS 0xc9

#define GETCALC_READ_TIMEOUT 10

/*
 * Read 'count' bytes into 'buf' from file 'fd'
 * Return only when all bytes have been read,
 * or longjmp to getcalcjmp on timeout
 */
ssize_t getcalcread(int fd, void *buf, size_t count)
{
	ssize_t n = 0;
	struct itimerval it = {
		{ 0, 0 },
		{ GETCALC_READ_TIMEOUT, 0 },
	};
	while (count > 0) {
		setitimer(ITIMER_REAL, &it, NULL);
		n = read(fd, buf, count);
		setitimer(ITIMER_REAL, NULL, NULL); /* clear timer */
		if (n < 0) longjmp(P.user.getcalcjmp, 1);
		count -= n;
		buf += n;
	};
	return n;
}

static unsigned cksum(unsigned char *buf, ssize_t count)
{
	unsigned sum = 0;
	unsigned char *p = buf;
	for (; count; --count)
		sum += *p++;
	sum &= 0xffff;
	return sum;
}

static int recvpkthead(struct pkthead *pkt, int fd)
{
	unsigned char head[4];
	ssize_t n;
	int machid, commid;
	unsigned length;
	const struct machinfo *mip;
	const struct comminfo *cip;
	
	/* read packet header */
	n = getcalcread(fd, head, 4);
	if (n < 4) return -1;
	
	machid = head[0];
	commid = head[1];
	length = head[2] | (head[3] << 8);
	for (mip = &machinfo[0]; mip->id != -1; ++mip)
		if (mip->id == machid) break;
	for (cip = &comminfo[0]; cip->id != -1; ++cip)
		if (cip->id == commid) break;
	//printf("machine id: 0x%02x (%s)\n", machid, mip->id == -1 ? "unknown" : mip->desc);
	//printf("command id: 0x%02x (%s)\n", commid, cip->id == -1 ? "unknown" : cip->desc);
	//printf("    length: %u\n", length);
	pkt->machid = machid;
	pkt->commid = commid;
	pkt->length = length;
	return 0;
}

static long recvpkt(struct pkthead *pkt, char *buf, size_t count, int fd)
{
	ssize_t n;
	unsigned length;
	unsigned checksum;
	unsigned char *p;
	unsigned buf2[2];
	const struct comminfo *cip;
	
	recvpkthead(pkt, fd);
	
	for (cip = &comminfo[0]; cip->id != -1; ++cip)
		if (cip->id == pkt->commid) break;
	
	if (!cip->hasdata) return 0;
	/* continue reading "length" bytes plus the checksum */
	length = pkt->length;
	if (length > count) return -1;
	p = buf;
	while (length > 0) {
		n = getcalcread(fd, p, length);
		if (n <= 0) return -1;
		length -= n;
		p += n;
	}
	n = getcalcread(fd, buf2, 2); /* checksum */
	checksum = buf2[0] | (buf2[1] << 8);
	printf("checksum: %u (%s)\n", checksum, checksum == cksum(buf, pkt->length) ? "correct" : "INCORRECT");
	return checksum;
}

static int sendpkthead(const struct pkthead *pkt, int fd)
{
	char buf[4];
	buf[0] = pkt->machid;
	buf[1] = pkt->commid;
	buf[2] = pkt->length % 256;
	buf[3] = pkt->length / 256;
	if (write(fd, buf, 4) < 4)
		return -1;
	return 0;
}

/* read and discard len bytes and return the checksum */
static long discard(int fd, unsigned len)
{
	unsigned char buf[128];
	unsigned sum = 0;
	ssize_t n;
	int i;
	while (len) {
		n = getcalcread(fd, buf, len < sizeof(buf) ? len : sizeof(buf));
		//printf("discard: read %ld bytes\n", n);
		for (i = 0; i < n; ++i)
			sum += buf[i];
		len -= n;
	}
	return sum;
}

static unsigned short getchecksum(int fd)
{
	unsigned char buf[2];
	if (getcalcread(fd, buf, 2) < 2) return -1;
	return buf[0] | (buf[1] << 8);
}

/* simple variable receiver */
static ssize_t getcalc(int fd, char *buf, size_t size)
{
	struct pkthead pkt;
	static const struct pkthead ackpkt = { 0x88, PKT_COMM_ACK, 0 };
	static const struct pkthead ack92ppkt = { 0x88, PKT_COMM_ACK, 0x1001 };
	static const struct pkthead ctspkt = { 0x88, PKT_COMM_CTS, 0 };
	static const struct pkthead errpkt = { 0x88, PKT_COMM_ERR, 0 };
	long n;
	unsigned short sum, checksum;
	ssize_t ret = 0;
	enum {
		RECV_START, RECV_WAITDATA, RECV_RXDATA
	} state = RECV_START;
	
	struct sigaction sa;
	sa.sa_handler = sigalrm;
	sa.sa_flags = 0;
	printf("sigaction returned %d\n", sigaction(SIGALRM, &sa, NULL));
	
	if (setjmp(P.user.getcalcjmp))
	{
		printf("TIMEOUT\n");
		return -1;
	}

	for (;;) {
		n = recvpkthead(&pkt, fd);
		switch (state) {
		case RECV_START:
			printf("START\n");
			if (pkt.commid == PKT_COMM_VAR || pkt.commid == PKT_COMM_RTS) {
				n = discard(fd, pkt.length);
				sum = n;
				n = getchecksum(fd);
				checksum = n;
				if (checksum == sum) {
					sendpkthead(&ackpkt, fd);
					sendpkthead(&ctspkt, fd);
					state = RECV_WAITDATA;
				} else {
					sendpkthead(&errpkt, fd);
				}
			} else if (pkt.commid == PKT_COMM_RDY) {
				if (pkt.machid == 0x00) {
					sendpkthead(&ack92ppkt, fd);
				} else {
					sendpkthead(&ackpkt, fd);
				}
			} else if (pkt.commid == PKT_COMM_EOT) {
				sendpkthead(&ackpkt, fd);
				return ret;
			} else {
				goto error;
			}
			break;
		case RECV_WAITDATA:
			printf("WAITDATA\n");
			if (pkt.commid != PKT_COMM_ACK)
				goto error;
			state = RECV_RXDATA;
			break;
		case RECV_RXDATA:
			printf("RXDATA\n");
			if (pkt.commid != PKT_COMM_DATA)
				goto error;
			//printf("reading and discarding variable data...\n");
			if (pkt.length > size) return -1;
			n = getcalcread(fd, buf, pkt.length);
			ret = n;
			sum = cksum(buf, n);
			//printf("reading variable data checksum...\n");
			n = getchecksum(fd);
			checksum = n;
			if (checksum == sum) {
				//printf("sending ACK\n");
				sendpkthead(&ackpkt, fd);
				state = RECV_START;
			} else {
				printf("sending ERR (checksum=0x%04x, expected=0x%04x\n", checksum, sum);
				sendpkthead(&errpkt, fd);
			}
			break;
		default:
			printf("getcalc internal error (%d)\n", state);
			return -1;
		}
		//userpause();
	}
error:
	printf("ERROR\n");
	//send something??
	return -1;
}

#define BPERLINE 8
static void printhex(char buf[], int length)
{
	int i, j;
	for (i = 0; i < (length + BPERLINE - 1) / BPERLINE * BPERLINE; ++i) {
		if (i < length)
			printf("%02x ", buf[i]);
		else
			printf("   ");
		if ((i + 1) % BPERLINE == 0) {
			for (j = i - BPERLINE + 1; j <= i; ++j) {
				int ch;
				ch = j < length ? buf[j] : ' ';
				if (ch < 31 || 126 < ch)
					ch = '.';
				putchar(ch);
			}
			putchar('\n');
		}
	}
	putchar('\n');
}

/*
 * 9xi variable format (reverse-engineered)
 * (excludes 9xi header)
 * 00 00 part of packet?
 * 00 00 part of packet?
 *
 * 0c 17 bytes to read (3095)
 * 00 67 height (103)
 * 00 ef width (239) // not 240??
 *
 * 103*240/8 bytes bitmap (row-major order)
 *
 * df    end mark
 */

#define LINKBUFSIZE (3840)
static void testlink(int argc, char *argv[], char *envp[])
{
	unsigned char *buf = NULL;
	int linkfd;
	struct pkthead packet;
	ssize_t n;
	
	linkfd = open("/dev/link", O_RDWR);
	printf("linkfd = %d\n", linkfd);
	if (linkfd < 0) goto out;
	
	printf("Receiving variable...\n");
	buf = malloc(LINKBUFSIZE);
	if (!buf) {
		printf("testlink(): could not allocate buf\n");
		return;
	}
	memset(buf, 0x42, LINKBUFSIZE);
	n = getcalc(linkfd, buf, LINKBUFSIZE);
	printf("getcalc returns %ld\n", n);
	if (n >= 0) {
		userpause();
		clear();
		memmove((void *)0x4c00L, buf+10, n-11);
		alarm(5);
		pause();
	}
	userpause();
	alarm(1);
	pause();
	
	printf("closing link...\n");
	close(linkfd);
	printf("closed.\n");
	
out:
	free(buf);
	userpause();
}

static int banner(const char *s)
{
#if CALC_HAS_LARGE_SCREEN
	static const char stars[] = "*****************************";
#else
	static const char stars[] = "*******************";
#endif
	int h;
	int l = strlen(s);
	h = l / 2;
	return printf("\n%s %s %s\n", &stars[h + (l%2)], s, &stars[h]);
}

struct test {
	char name[20];
	void (*func)();
};

static const struct test tests[] = {
	{ "nice", testnice },
	{ "sr64", testsr64 },
	{ "fpu", testfpu },
	{ "strtol", teststrtol },
	{ "clock", testclock },
	{ "random", testrandom },
	{ "link", testlink },
	{ "audio", testaudio },
	{ "", NULL }
};

#define BUFSIZE 512

static int sysctltest_main(int argc, char **argv, char **envp)
{
	int mib[] = { CTL_KERN, KERN_NGROUPS };
	unsigned miblen = sizeof(mib) / sizeof(mib[0]);
	long value;
	size_t valuelen = sizeof(long);
	if (sysctl(mib, miblen, &value, &valuelen, NULL, 0)) {
		perror("sysctltest");
		return 1;
	}
	printf("sysctltest: %ld\n", value);
	
	return 0;
}

static int ps_main(int argc, char **argv, char **envp)
{
	int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
	unsigned miblen = sizeof(mib) / sizeof(mib[0]);
	struct kinfo_proc *kp = NULL;
	struct kinfo_proc *allproc = NULL;
	size_t allproclen = 0;
	int status = 0;
	if (sysctl(mib, miblen, NULL, &allproclen, NULL, 0L)) {
		perror("ps");
		status = 1;
		goto out;
	}
	allproc = malloc(allproclen);
	if (!allproc) {
		perror("ps");
		status = 1;
		goto out;
	}
	if (sysctl(mib, miblen, allproc, &allproclen, NULL, 0L)) {
		perror("ps");
		status = 1;
		goto out_free;
	}
	printf("%5s %4s %8s %s\n", "PID", "TTY", "TIME", "CMD");
	allproclen /= sizeof(struct kinfo_proc);
	for (kp = &allproc[0]; kp < &allproc[allproclen]; ++kp) {
		long s;
		int h, m;
		s = kp->kp_ctime / CLOCKS_PER_SEC;
		h = s / 3600;
		s %= 3600;
		m = s / 60;
		s %= 60;
		printf("%5d %04x %02d:%02d:%02ld %s%s\n",
		       kp->kp_pid, kp->kp_tty, h, m, s, kp->kp_cmd,
		       kp->kp_state == PZOMBIE ? " <defunct>" : "");
	}
out_free:
	free(allproc);
out:
	return status;
}

static int bt_main(int argc, char **argv, char **envp)
{
#define BTBUFSIZ 20
	void *buffer[BTBUFSIZ];
	void **p;
	int size;
	int backtrace(void **buffer, int size);
	size = backtrace(&buffer[0], BTBUFSIZ);
	printf("%d frames:\n", size);
	for (p = &buffer[0]; size; --size, ++p)
		printf("%p\n", *p);
	return 0;
}

static int crash_main(int argc, char **argv, char **envp)
{
	/*
.global buserr
.global SPURIOUS
.global ADDRESS_ERROR
.global ILLEGAL_INSTR
.global ZERO_DIVIDE
.global CHK_INSTR
.global I_TRAPV
.global PRIVILEGE
.global TRACE
.global LINE_1010
.global LINE_1111
*/
	char *type;
	if (argc != 2) {
		printf("Usage: %s type\n", argv[0]);
		printf("where type is one of:\n"
		       //"  bus      bus error\n"
		       "  address  address error\n"
		       "  illegal  illegal instruction\n"
		       "  zerodiv  division by zero\n"
		       "  chk      chk instruction\n"
		       "  trapv    trap on overflow\n"
		       "  priv     privileged instruction\n"
		       //"  trace    instruction tracing\n"
		       "  1010     line 1010 emulator\n"
		       "  1111     line 1111 emulator\n"
		       "  segv     invalid memory access\n");
		return 1;
	}

	type = argv[1];
	//if (!strcasecmp(type, "buserr")) {
		//asm(" jsr 0x4321 "); // FIXME
	//} else
	if (!strcasecmp(type, "address")) {
		asm(" move #42,0x4321 ");
	} else if (!strcasecmp(type, "illegal")) {
		asm(" illegal ");
	} else if (!strcasecmp(type, "zerodiv")) {
		asm(" divu #0,%d0 ");
	} else if (!strcasecmp(type, "chk")) {
		asm(" move #5,%d0; chk #4,%d0 ");
	} else if (!strcasecmp(type, "trapv")) {
		asm(" move #0x7fff,%d0; add #1,%d0; trapv ");
	} else if (!strcasecmp(type, "priv")) {
		asm(" move #0x2700,%sr ");
	//} else if (!strcasecmp(type, "trace")) {
		//asm(" trace ");
	} else if (!strcasecmp(type, "1010")) {
		asm(" .word 0xa000 ");
	} else if (!strcasecmp(type, "1111")) {
		// this won't crash if fpuemu is enabled
		asm(" .word 0xf000; nop; nop ");
	} else if (!strcasecmp(type, "segv")) {
		*(long *)2 = 42;
	} else {
		printf("%s: unknown crash type %s\n", argv[0], type);
		return 1;
	}

	printf("Look at that! We didn't crash!\n");
	return 0;
}

typedef unsigned long int64[2];
typedef unsigned long int128[4];

static void test_mul(int64 a, int64 b)
{
	int128 r;
	
	void mul64(int64 a, int64 b, int128 result);
	mul64(a, b, r);

	printf("%08lx%08lx * %08lx%08lx =\n"
	       "%08lx%08lx%08lx%08lx\n",
	       a[0], a[1], b[0], b[1],
	       r[0], r[1], r[2], r[3]);
}

static int mul_main(int argc, char **argv, char **envp)
{

	int64 tests[][2] = {
		{ { 0x000000E8,0xD4A51000 }, { 0x00000000,0x0280DE80 } },
		{ { 0xDEADFACE,0xBEEFF00D }, { 0xCAFEBABE,0x0B00B1E5 } },
		{ { 0xB504F333,0xf9de6484 }, { 0xB504F333,0xf9de6484 } },
	};
	int i;

	for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i)
		test_mul(tests[i][0], tests[i][1]);

	return 0;
}

static void test_div(int64 a, int64 b)
{
	int64 r;
	
	void div64(int64 a, int64 b, int64 result);
	div64(a, b, r);

	printf("%08lx%08lx / %08lx%08lx =\n"
	       "%08lx%08lx\n",
	       a[0], a[1], b[0], b[1],
	       r[0], r[1]);
}

static int div_main(int argc, char **argv, char **envp)
{
	int64 tests[][2] = {
		{ { 0x80000000,0x00000000 }, { 0x80000000,0x00000000 }, },
		{ { 0x80000000,0x00000000 }, { 0xffffffff,0xffffffff }, },
		{ { 0xffffffff,0xffffffff }, { 0xffffffff,0xffffffff }, },
		{ { 0xffffffff,0xffffffff }, { 0x80000000,0x00000000 }, },
		{ { 0xC90FDAA2,0x20000000 }, { 0xA0000000,0x00000000 }, },
	};
	int i;

	for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i)
		test_div(tests[i][0], tests[i][1]);

	return 0;
}

static int fdtofd(int fromfd, int tofd)
{
	char buf[BUFSIZE];
	ssize_t n, x;
	char *bp;
	n = read(fromfd, buf, BUFSIZE);
	if (n < 0)
		return n;
	//printf("fdtofd: read %ld bytes from fd %d\n", n, fromfd);
	x = n;
	bp = buf;
	while (x > 0) {
		ssize_t y = write(tofd, bp, x);
		//printf("fdtofd: wrote %ld bytes to fd %d\n", y, tofd);
		if (y < 0 && errno != EINTR) return -1;
		x -= y;
		bp += y;
	}
	return n;
}

int uterm_main(int argc, char **argv, char **envp)
{
	int linkfd;
	struct sigaction sa;
	/* periodically interrupt read() calls */
	struct itimerval it = {
		{ 0, 1000000/8 },
		{ 0, 1000000/8 }
	};

	linkfd = open("/dev/link", O_RDWR);
	if (linkfd < 0) {
		perror("/dev/link");
		return 1;
	}

	sa.sa_handler = sigalrm;
	//sa.sa_flags = SA_RESTART;
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, NULL);
	setitimer(ITIMER_REAL, &it, NULL);
	
	for (;;) {
		ssize_t x, y;
		do {
			x = fdtofd(linkfd, 1);
		} while (x > 0);
		do {
			y = fdtofd(0, linkfd);
		} while (y > 0);
		if ((x < 0 || y < 0) && errno != EINTR) {
			perror("uterm");
			break;
		}
		if (x == 0) break;
	}
	close(linkfd);

	return 0;
}

static int poweroff_main(int argc, char **argv, char **envp)
{
	poweroff();
	return 0;
}

int time_main(int argc, char **argv, char **envp)
{
	int posix = 0;
	int pid;
	int err = 0;
	char **v;
	struct timeval start, end;
	struct tms tms;
	struct timeval childu, childs;
	v = argv;
	if (argc > 1 && !strcmp(argv[1], "-p")) {
		posix = 1;
		++v;
		--argc;
	}
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [-p] utility [argument...]\n", argv[0]);
		return 1;
	}
	++v;

	gettimeofday(&start, NULL);
	pid = vfork();
	if (pid == 0) {
		int err;
		execve(v[0], v, envp);
		if (errno == ENOENT)
			err = 127;
		else
			err = 126;
		printf("%s: cannot run %s: %s\n", argv[0], v[0], strerror(errno));
		_exit(err);
	} else if (pid > 0) {
		int status;
		for (;;) {
			if (wait(&status) < 0) {
				err = 1;
				break;
			} else if (WIFEXITED(status)) {
				err = WEXITSTATUS(status);
				break;
			} else if (WIFSIGNALED(status)) {
				err = WTERMSIG(status);
				break;
			}
		}
	} else {
		perror("time");
		return 1;
	}
	gettimeofday(&end, NULL);
	times(&tms);
	
	timersub(&end, &start, &end);
	clock_t_to_timeval(tms.tms_cutime, &childu);
	clock_t_to_timeval(tms.tms_cstime, &childs);
	
	printf("real %ld.%03ld\n"
	       "user %ld.%03ld\n"
	       "sys %ld.%03ld\n",
	       end.tv_sec, end.tv_usec/1000,
	       childu.tv_sec, childu.tv_usec/1000,
	       childs.tv_sec, childs.tv_usec/1000);

	return err;
}

static void printru(int which)
{
	struct rusage ru;
	long minu, secu, msecu;
	long mins, secs, msecs;
	getrusage(which, &ru);
	minu = ru.ru_utime.tv_sec / 60;
	secu = ru.ru_utime.tv_sec % 60;
	msecu = ru.ru_utime.tv_usec / 1000;
	mins = ru.ru_stime.tv_sec / 60;
	secs = ru.ru_stime.tv_sec % 60;
	msecs = ru.ru_stime.tv_usec / 1000;
	printf("%ldm%ld.%03lds %ldm%ld.%03lds\n",
	       minu, secu, msecu,
	       mins, secs, msecs);
}

static int times_main(int argc, char **argv, char **envp)
{
	printru(RUSAGE_SELF);
	printru(RUSAGE_CHILDREN);
	return 0;
}

int tests_main(int argc, char **argv, char **envp)
{
	const struct test *testp;
	int match = 0;
	for (testp = &tests[0]; testp->func; ++testp) {
		if (argc <= 1 || !strcmp(argv[1], testp->name)) {
			match = 1;
			banner(testp->name);
			testp->func(argc - 1, argv + 1, envp);
		}
	}
	if (!match) {
		printf("error: no test named %s\n", argv[1]);
		return 1;
	}
	printf("done!\n");
	return 0;
}

static int docat(int fd)
{
	char buf[BUFSIZE];
	ssize_t n;
	for (;;) {
		n = read(fd, buf, BUFSIZE);
		if (n <= 0) break;
		n = write(1, buf, n);
		if (n <= 0) break;
	}
	return (n < 0);
}

int cat_main(int argc, char **argv, char **envp)
{
	int err = 0;
	int fd;
	int i;
	
	if (argc <= 1)
		return docat(0);
	
	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-")) {
			fd = 0; /* stdin */
		} else {
			fd = open(argv[i], O_RDONLY);
		}
		if (fd < 0) {
			printf("cat: %s: %s\n", argv[i], strerror(errno));
			err = 1;
			continue;
		}
		if (docat(fd))
			err = 1;
		if (fd != 0) close(fd);
	}
	return err;
}

static int echo_main(int argc, char **argv, char **envp)
{
	int i;
	if (argc >= 2)
		printf("%s", argv[1]);
	for (i = 2; i < argc; ++i)
		printf(" %s", argv[i]);
	putchar('\n');
	return 0;
}

static int false_main(int argc, char **argv, char **envp)
{
	return 1;
}

static int true_main(int argc, char **argv, char **envp)
{
	return 0;
}

static int clear_main(int argc, char **argv, char **envp)
{
	clear();
	return 0;
}

static int uname_main(int argc, char **argv, char **envp)
{
	struct utsname utsname;
	int err;
	
	err = uname(&utsname);
	if (!err) {
		printf("%s %s %s %s %s\n",
		       utsname.sysname,
		       utsname.nodename,
		       utsname.release,
		       utsname.version,
		       utsname.machine);
	} else {
		printf("uname returned %d\n", err);
	}
	return 0;
}

static int env_main(int argc, char **argv, char **envp)
{
	char **ep = envp;
	for (; *ep; ++ep)
		printf("%s\n", *ep);
	return 0;
}

static int id_main(int argc, char **argv, char **envp)
{
	uid_t uid;
	gid_t gid;
	gid_t groups[15];
	int numgroups, i;
	
	uid = getuid();
	gid = getgid();
	numgroups = getgroups(15, groups);
	printf("uid=%d gid=%d groups=%d", uid, gid, gid);
	for (i = 0; i < numgroups; ++i) {
		printf(",%d", groups[i]);
	}
	putchar('\n');
	return 0;
}

static int pause_main(int argc, char **argv, char **envp)
{
	struct sigaction sa;
	int e;
	sa.sa_handler = sigalrm;
	//sa.sa_flags = SA_RESTART;
	sa.sa_flags = 0;
	printf("sigaction returned %d\n", sigaction(SIGALRM, &sa, NULL));
	alarm(3);
	e = pause();
	perror("pause");
	return 0;
}

static int batt_main(int argc, char **argv, char **envp)
{
	static const char
	  *batt_level_strings[8] = {
	    "dead",
	    "mostly dead",
	    "starving",
	    "very low",
	    "low",
	    "medium",
	    "ok",
	    "full"
	  };
	printf("Battery level: %d (%s)\n", G.batt_level,
	       batt_level_strings[G.batt_level]);
	return 0;
}

static int date_main(int argc, char **argv, char **envp)
{
	/*
	 * this prints out the equivalent of
	 * date --utc '+%F %T %Z'
	 */
	struct tm tm;
	struct timeval tv;
	char buf[40];
		
	gettimeofday(&tv, NULL);
	//printf("%5ld.%06ld\n", tv.tv_sec, tv.tv_usec);
	localtime_r(&tv.tv_sec, &tm);
#if 0
	printf("%4d-%02d-%02d %02d:%02d:%02d UTC\n",
	 tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
	 tm.tm_hour, tm.tm_min, tm.tm_sec);
#else
	printf("%s", asctime_r(&tm, buf));
#endif
	return 0;
}

static int adjtime_main(int argc, char **argv, char **envp)
{
	struct timeval tv = { 10, 0 };
	struct timeval oldtv;
	adjtime(&tv, &oldtv);

#define FIXTV(tv) do { \
if ((tv)->tv_sec < 0 && (tv)->tv_usec) { \
++(tv)->tv_sec; \
(tv)->tv_usec = 1000000L - (tv)->tv_usec; \
} \
} while (0)

	FIXTV(&tv);
	FIXTV(&oldtv);
	printf("previous time adjustment: %ld.%06lu\n",
	       oldtv.tv_sec, oldtv.tv_usec);
	printf("new time adjustment: %ld.%06lu\n",
	       tv.tv_sec, tv.tv_usec);
	return 0;
}

static int malloc_main(int argc, char **argv, char **envp)
{
	void *x;
	size_t s = 256*1024UL;
	while (s && !(x = malloc(s)))
		s -= 1024;
	free(x);
	printf("allocated up to %lu contiguous bytes\n", s);
	return 0;
}

static int pid_main(int argc, char **argv, char **envp)
{
	printf("%d\n", getpid());
	return 0;
}

static int pgrp_main(int argc, char **argv, char **envp)
{
	printf("%d\n", getpgrp());
	return 0;
}

int nice_main(int argc, char **argv, char **envp)
{
	int err;
	int inc = 10; // default increment
	int cmdarg = 1;
	if (argc == 1) {
		printf("%d\n", nice(0));
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "-n")) {
		inc = atoi(argv[2]);
		cmdarg = 3;
	}
	if (argc <= cmdarg) {
		printf("nice: command must be given\n");
		return 1;
	}
	errno = 0;
	if (nice(inc) == -1 && errno) {
		perror("nice");
	}
	execve(argv[cmdarg], &argv[cmdarg], envp);
	if (errno == ENOENT)
		err = 127;
	else
		err = 126;
	perror(argv[cmdarg]);
	return err;
}

int atoi(const char *a)
{
	int i = 0;
	int neg = 0;
	while (isspace(*a)) {
		++a;
	}
	switch (*a) {
	case '-': neg = 1;
	case '+': ++a;
	}
	while (isdigit(*a)) {
		i = 10 * i + (*a++ - '0');
	}
	if (neg)
		i = -i;
	return i;
}

static int kill_main(int argc, char **argv, char **envp)
{
/*
 * kill -s signal_name pid ...
 * kill -l [exit_status]
 * kill [-signal_name] pid ...
 * kill [-signal_number] pid ...
 */
	static const char *const names[] = {
		[0] = "0",
		[SIGHUP] = "SIGHUP",      // 1
		[SIGINT] = "SIGINT",      // 2
		[SIGQUIT] = "SIGQUIT",    // 3
		[SIGILL] = "SIGILL",
		[SIGTRAP] = "SIGTRAP",
		[SIGABRT] = "SIGABRT",    // 6
		[SIGEMT] = "SIGEMT",
		[SIGFPE] = "SIGFPE",
		[SIGKILL] = "SIGKILL",    // 9
		[SIGBUS] = "SIGBUS",
		[SIGSEGV] = "SIGSEGV",
		[SIGSYS] = "SIGSYS",
		[SIGPIPE] = "SIGPIPE",
		[SIGALRM] = "SIGALRM",    // 14
		[SIGTERM] = "SIGTERM",    // 15
		[SIGURG] = "SIGURG",
		[SIGSTOP] = "SIGSTOP",
		[SIGTSTP] = "SIGTSTP",
		[SIGCONT] = "SIGCONT",
		[SIGCHLD] = "SIGCHLD",
		[SIGTTIN] = "SIGTTIN",
		[SIGTTOU] = "SIGTTOU",
#ifdef SIGIO
		[SIGIO] = "SIGIO",
#elif defined(SIGPOLL)
		[SIGPOLL] = "SIGPOLL",
#endif
		[SIGXCPU] = "SIGXCPU",
		[SIGXFSZ] = "SIGXFSZ",
		[SIGVTALRM] = "SIGVTALRM",
		[SIGPROF] = "SIGPROF",
		[SIGWINCH] = "SIGWINCH",
		[SIGINFO] = "SIGINFO",
		[SIGUSR1] = "SIGUSR1",
		[SIGUSR2] = "SIGUSR2",
	};
#define SIZEOF_NAMES (sizeof(names)/sizeof(names[0]))
	void usage() {
		fprintf(stderr, "Usage: kill -s signal_name pid ...\n"
		                "       kill -l [exit_status]\n"
		                "       kill [-signal_name] pid ...\n"
		                "       kill [-signal_number] pid ...\n");
	}
	int signum(const char *signame) {
		int i;
		for (i = 0; i < SIZEOF_NAMES; ++i) {
			if (!names[i]) continue;
			if (!strcmp(signame, names[i]) ||
			    !strcmp(signame, names[i]+3)) {
				return i;
			}
		}
		return -1;
	}

	int sig = SIGTERM;
	int pidarg = 1;
	pid_t pid = -1;
	int i;
	int err = 0;

	if (argv[1][0] == '-') {
		if (!strcmp(argv[1], "-s")) {
			/* kill -s signal_name pid ... */
			if (argc < 4) {
				fprintf(stderr, "kill: not enough arguments\n");
				usage();
				return 1;
			}
			sig = signum(argv[2]);
			if (sig < 0) {
				fprintf(stderr, "kill: unknown signal \"%s\"\n",
				       argv[2]);
				return 1;
			}
			pidarg = 3;
		} else if (!strcmp(argv[1], "-l")) {
			/* kill -l [exit_status] */
			if (argc > 3) {
				fprintf(stderr, "kill: too many arguments\n");
				usage();
				return 1;
			}
			if (argc == 3)
				sig = atoi(argv[2]);
			else
				sig = -1;
			if (sig > 0) {
				int s = sig;
				if (128 < s && s < SIZEOF_NAMES + 128)
					s -= 128;
				if (0 < s && s < SIZEOF_NAMES && names[s]) {
					printf("%s\n", names[s] + 3);
					return 0;
				}
				fprintf(stderr, "kill: unknown signal %d\n", sig);
				return 1;
			}
			for (i = 1; i < SIZEOF_NAMES; ++i) {
				if (!names[i]) continue;
				printf("%2d %-12s", i, names[i]);
			}
			printf("\n");
			return 0;
		} else if (!strcmp(argv[1], "--")) {
			pidarg = 2;
		} else {
			/* kill -signal_name pid ... */
			/* kill -signal_number pid ... */
			sig = atoi(&argv[1][1]);
			if (sig == 0) {
				sig = signum(&argv[1][1]);
			}
			if (sig < 0) {
				fprintf(stderr, "kill: unknown signal \"%s\"\n",
				        &argv[1][1]);
				return 1;
			}
			pidarg = 2;
		}
	}
	if (pidarg >= argc) {
		usage();
		return 1;
	}
	for (; pidarg < argc; ++pidarg) {
		pid = atoi(argv[pidarg]);
		if (kill(pid, sig)) {
			err = 1;
			perror("kill");
		}
	}
	return err;
}

static int exec_command(int ch)
{
	switch (ch) {
	case 'q': return 1;
	case ' ': case '\n': break;
	default: printf("\rUnknown command '%c'", ch); alarm(2); pause(); break;
	}
	return 0;
}

struct topinfo {
	struct timeval lasttime;
	struct rusage lastrusage;
};

#if 0
static void updatetop(struct topinfo *info)
{
	int getloadavg1(long loadavg[], int nelem);
	struct timeval tv;
	struct timeval difftime;
	long la[3];
	int day, hour, minute, second;
	long msec;
	long umsec, smsec;
	long t;
	int i;
	int ucpu = 42;
	int scpu = 31;
	int totalcpu;
	int idle;
	int pid;
	struct rusage rusage;
	struct timeval ptime;
	struct timeval diffutime;
	struct timeval diffstime;
	
	getrusage(RUSAGE_SELF, &rusage);
	gettimeofday(&tv, NULL);
	
	if (info->lasttime.tv_sec != 0 || info->lasttime.tv_usec != 0) {
		timersub(&tv, &info->lasttime, &difftime);
		timersub(&rusage.ru_utime, &info->lastrusage.ru_utime, &diffutime);
		timersub(&rusage.ru_stime, &info->lastrusage.ru_stime, &diffstime);
		timeradd(&rusage.ru_utime, &rusage.ru_stime, &ptime);
		msec = difftime.tv_sec * 1000L + difftime.tv_usec / 1000;
		if (msec == 0) return;
		umsec = diffutime.tv_sec * 1000L + diffutime.tv_usec / 1000;
		smsec = diffstime.tv_sec * 1000L + diffstime.tv_usec / 1000;
		ucpu = (1000L * umsec + 500) / msec;
		scpu = (1000L * smsec + 500) / msec;
		totalcpu = ucpu+scpu;
		/* totalcpu is occasionally > 100% */
		if (totalcpu > 1000) totalcpu = 1000;
		idle = 1000 - totalcpu;
		getloadavg1(la, 3);
		
		/* line 1 */
		t = tv.tv_sec - 25200; /* -7 hours */
		second = t % 60; t /= 60;
		minute = t % 60; t /= 60;
		hour = t % 24;   t /= 24;
		day = t;
		printf(ESC "[H" "%02d:%02d:%02d up ",
		       hour, minute, second);
		t = uptime.tv_sec;
		second = t % 60; t /= 60;
		minute = t % 60; t /= 60;
		hour = t % 24;   t /= 24;
		day = t;
		if (day) {
			printf("%d+", day);
		}
		printf("%02d:%02d, 1 user, load:", hour, minute);
		for (i = 0; i < 3; ++i) {
			if (i > 0) putchar(',');
			printf(" %ld.%02ld", la[i] >> 16,
			       (100 * la[i] >> 16) % 100);
		}
		cleareol();
		/* line 2 */
		printf("\nTasks: 1 total, 1 run, 0 slp, 0 stop, 0 zomb");
		cleareol();
		/* line 3 */
		printf("\nCpu(s): %3d.%01d%%us, %3d.%01d%%sy, %3d.%01d%%ni, %3d.%01d%%id", ucpu/10, ucpu%10, scpu/10, scpu%10, 0, 0, idle/10, idle%10);
		cleareol();
		/* line 4 */
		printf("\nMem: 0k total, 0k used, 0k free, 0k buffers");
		cleareol();
		/* line 5 */
		putchar('\n');
		cleareol();
		/* line 6 */
		printf("\n" ESC "[7m  PID USER      PR  NI  SHR  RES S  %%CPU   TIME COMMAND" ESC "[m");
		cleareol();
		/* line 7- */
		pid = getpid();
		/* BOGUS! we can't really use 'current' in userspace */
		printf("\n%5d %-8s %3d %3d %3ldk %3ldk %c %3d.%01d %3d:%02d %.12s",
		       pid, "root", 0,
		       getpriority(PRIO_PROCESS, pid), (long)0, (long)0, 'R',
		       current->p_pctcpu / 256, (current->p_pctcpu % 256) * 10 / 256,
		       (int)(ptime.tv_sec / 60), (int)(ptime.tv_sec % 60),
		       current->p_name);
		cleareol();
		printf(ESC "[J" ESC "[5H");
	} else {
		printf("reticulating splines...");
	}
	info->lasttime = tv;
	info->lastrusage = rusage;
}
#else

static int topcompare_tty(const void *a, const void *b)
{
	struct kinfo_proc *const *proca = a, *const *procb = b;
	return (*proca)->kp_tty - (*procb)->kp_tty;
}

static int topcompare_pcpu(const void *a, const void *b)
{
	struct kinfo_proc *const *proca = a, *const *procb = b;
	return (*procb)->kp_pcpu - (*proca)->kp_pcpu; /* sort in descending order */
}

/* use sysctl() to get system and process information */
static int updatetop(struct topinfo *info)
{
	long t;
	int day, hour, minute, second;
	int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
	unsigned miblen = sizeof(mib) / sizeof(*mib);
	struct kinfo_proc *allproc = NULL;
	size_t allproclen;
	struct kinfo_proc **allprocp = NULL;
	struct kinfo_proc *kp;
	struct kinfo_proc **kpp;
	long cpu_user = 0, cpu_sys = 0, cpu_nice = 0, cpu_idle = 0;
	int upmib[] = { CTL_KERN, KERN_UPTIME };
	unsigned upmiblen = sizeof(upmib) / sizeof(*upmib);
	time_t up = 0;
	size_t uplen = sizeof(long);
	struct timeval tv;
	int i;
	long la[3];
	static const char procstates[] = "RSDTZ";
	int nprocs, nrun, nslp, nstop, nzomb;
	int nusers;
	dev_t lasttty = 0;
	int status = 0;
	nprocs = nrun = nslp = nstop = nzomb = nusers = 0;

	sysctl(upmib, upmiblen, &up, &uplen, NULL, 0L);
	gettimeofday(&tv, NULL);
	getloadavg32(la, 3);
	
	if (sysctl(mib, miblen, NULL, &allproclen, NULL, 0L)) {
		perror("top: sysctl 1");
		return 1;
	}
	allproc = malloc(allproclen);
	if (!allproc) {
		perror("top: malloc");
		return 1;
	}
	if (sysctl(mib, miblen, allproc, &allproclen, NULL, 0L)) {
		perror("top: sysctl 2");
		status = 1;
		goto free;
	}
	allproclen /= sizeof(*allproc);

	allprocp = malloc(allproclen * sizeof(void *));
	if (!allprocp) {
		perror("top: allprocp");
		status = 1;
		goto free;
	}
	/* use pointers to procs in allproc array for sorting etc */
	for (kpp = &allprocp[0], kp = &allproc[0];
	     kpp < &allprocp[allproclen]; ++kpp, ++kp) {
		*kpp = kp;
	}
	
	/* sort the array by tty */
	qsort(allprocp, allproclen, sizeof(void *), topcompare_tty);
	
	for (kpp = &allprocp[0]; kpp < &allprocp[allproclen]; ++kpp) {
		++nprocs;
		switch ((*kpp)->kp_state) {
		case PRUN: ++nrun; break;
		case PSLEEP: case PDSLEEP: ++nslp; break;
		case PSTOPPED: ++nstop; break;
		case PZOMBIE: ++nzomb; break;
		}
		/* count users by tty, excluding tty 0 (= no tty) */
		if ((*kpp)->kp_tty != lasttty) ++nusers;
		lasttty = (*kpp)->kp_tty;
		/* accumulate cpu usage */
		if ((*kpp)->kp_nice <= 0)
			cpu_user += (*kpp)->kp_pcpu;
		else
			cpu_nice += (*kpp)->kp_pcpu;
		/* TODO: count system time here */
	}
	cpu_idle = 100L * 256 - cpu_user - cpu_sys - cpu_nice;

	/* sort the array by cpu usage */
	qsort(allprocp, allproclen, sizeof(void *), topcompare_pcpu);
	
#if CALC_HAS_LARGE_SCREEN
	/* line 1 */
	t = tv.tv_sec - 25200; /* -7 hours */
	second = t % 60; t /= 60;
	minute = t % 60; t /= 60;
	hour = t % 24;   t /= 24;
	day = t;
	printf(ESC "[H" "%02d:%02d:%02d up ",
	       hour, minute, second);
	t = up;
	second = t % 60; t /= 60;
	minute = t % 60; t /= 60;
	hour = t % 24;   t /= 24;
	day = t;
	if (day) {
		printf("%d+", day);
	}
	printf("%02d:%02d, %d user%s, load:",
	       hour, minute, nusers, nusers == 1 ? "" : "s");
	for (i = 0; i < 3; ++i) {
		if (i > 0) putchar(',');
		printf(" %ld.%02ld", la[i] >> 16,
		       (100 * la[i] >> 16) % 100);
	}
	cleareol();
	/* line 2 */
	printf("\nTasks: %d total, %d run, %d slp, %d stop, %d zomb",
	       nprocs, nrun, nslp, nstop, nzomb);
	cleareol();
	/* line 3 */
	printf("\nCpu: %3ld.%ld%%us, %3ld.%ld%%sy, %3ld.%ld%%ni, %3ld.%ld%%id",
	       cpu_user / 256, (cpu_user % 256) * 10 / 256,
	       cpu_sys  / 256, (cpu_sys  % 256) * 10 / 256,
	       cpu_nice / 256, (cpu_nice % 256) * 10 / 256,
	       cpu_idle / 256, (cpu_idle % 256) * 10 / 256);
	cleareol();
	/* line 4 */
	printf("\nMem: %ldk total, %ldk used, %ldk free, %ldk buffers",
	       -1L, -1L, -1L, -1L);
	cleareol();
	/* line 5 */
	putchar('\n');
	cleareol();
	/* line 6 */
	printf("\n" ESC "[7m  PID USER      PR  NI  SHR  RES S  %%CPU   TIME COMMAND" ESC "[m");
	cleareol();
	/* line 7- */
	if (allproclen > 20 - 6) allproclen = 20 - 6; /* XXX constant */
	for (kpp = &allprocp[0]; kpp < &allprocp[allproclen]; ++kpp) {
		long s = (*kpp)->kp_ctime / CLOCKS_PER_SEC;
		long m = s / 60;
		s %= 60;
		printf("\n%5d %-8d %3d %3d %3ldk %3ldk %c %3d.%01d %3ld:%02ld %.12s",
		       (*kpp)->kp_pid, (*kpp)->kp_euid, 0,
		       (*kpp)->kp_nice, 0L, 0L, procstates[(*kpp)->kp_state],
		       (*kpp)->kp_pcpu / 256, ((*kpp)->kp_pcpu % 256) * 10 / 256,
		       m, s, (*kpp)->kp_cmd);
		cleareol();
	}
#else
	/* line 1 */
	t = up;
	second = t % 60; t /= 60;
	minute = t % 60; t /= 60;
	hour = t % 24;   t /= 24;
	day = t;
	printf(ESC "[H");
	if (day) {
		printf("%d+", day);
	}
	printf("%02d:%02d, %d user%s, load:",
	       hour, minute, nusers, nusers == 1 ? "" : "s");
	for (i = 0; i < 3; ++i) {
		if (i > 0) putchar(',');
		printf(" %ld.%02ld", la[i] >> 16,
		       (100 * la[i] >> 16) % 100);
	}
	cleareol();
	/* line 2 */
	printf("\n%d tasks, %d run, %d slp, %d stop, %d zomb",
	       nprocs, nrun, nslp, nstop, nzomb);
	cleareol();
	/* line 3 */
	printf("\n%3ld.%ld%%us, %3ld.%ld%%sy, %3ld.%ld%%ni, %3ld.%ld%%id",
	       cpu_user / 256, (cpu_user % 256) * 10 / 256,
	       cpu_sys  / 256, (cpu_sys  % 256) * 10 / 256,
	       cpu_nice / 256, (cpu_nice % 256) * 10 / 256,
	       cpu_idle / 256, (cpu_idle % 256) * 10 / 256);
	cleareol();
	/* line 4 */
	printf("\n%ldk ttl, %ldk used, %ldk free, %ldk buf",
	       -1L, -1L, -1L, -1L);
	cleareol();
	/* line 5 */
	putchar('\n');
	cleareol();
	/* line 6 */
	printf("\n" ESC "[7m  PID USER     RES S  %%CPU COMMAND" ESC "[m");
	cleareol();
	/* line 7- */
	if (allproclen > 20 - 6) allproclen = 20 - 6; /* XXX constant */
	for (kpp = &allprocp[0]; kpp < &allprocp[allproclen]; ++kpp) {
		long s = (*kpp)->kp_ctime / CLOCKS_PER_SEC;
		long m = s / 60;
		s %= 60;
		printf("\n%5d %-8d %3d %c %3d.%01d %.13s",
		       (*kpp)->kp_pid, (*kpp)->kp_euid, 0,
		       procstates[(*kpp)->kp_state],
		       (*kpp)->kp_pcpu / 256, ((*kpp)->kp_pcpu % 256)*10 / 256,
		       (*kpp)->kp_cmd);
		cleareol();
	}
#endif
	/* clear to the end of the screen */
	printf(ESC "[J" ESC "[5H");
	if (feof(stdout) || ferror(stdout))
		status = 1;
free:
	free(allproc);
	free(allprocp);
	return status;
}
#endif

#define TOPBUFSIZE 200
int top_main(int argc, char *argv[], char **envp)
{
	/* we should eventually put the terminal in raw mode
	 * so we can read characters as soon as they're typed */
	
	char buf[TOPBUFSIZE];
	ssize_t n;
	int quit = 0;
	struct topinfo info;
#define TOPDELAY 5
	struct itimerval it = {
		{ TOPDELAY, 0 },
		{ TOPDELAY, 0 }
	};
	struct sigaction sa;
	
	sa.sa_handler = sigalrm;
	//sa.sa_flags = SA_RESTART;
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, NULL);
	setitimer(ITIMER_REAL, &it, NULL);
	
	info.lasttime.tv_sec = info.lasttime.tv_usec = 0;
	while (!quit) {
		if (updatetop(&info))
			return 1;
		n = read(0, buf, TOPBUFSIZE);
		if (n >= 0) {
			size_t i;
			n = n > 0; /* discard all but the first character */
			for (i = 0; i < n; ++i)
				if (exec_command(buf[i]))
					quit = 1;
			setitimer(ITIMER_REAL, &it, NULL);
		}
	}
	
	clear();
	
	return 0;
}

static int run(const char *, int, char **, char **);
struct applet {
	const char *name;
	int (*main)(int argc, char **argv, char **envp);
};
static struct applet applets[];

static void showhelp(int shell)
{
	struct applet *ap;
	fprintf(stderr, "available applets:\n");
	for (ap = &applets[0]; ap->name; ++ap) {
		if (!shell && ap->main == NULL) continue;
		fprintf(stderr, " %-9s", ap->name);
	}
	fprintf(stderr, "\n");
}

const unsigned char _ctype[256] = {
	_C, _C, _C, _C, _C, _C, _C, _C, _C,_C|_S,_C|_S,_C|_S,_C|_S,_C|_S,_C,_C,
	_C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C, _C,
	_S, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P, _P,
	_N, _N, _N, _N, _N, _N, _N, _N, _N, _N, _P, _P, _P, _P, _P, _P,
	_P, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U,
	_U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _U, _P, _P, _P, _P, _P,
	_P, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L,
	_L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _L, _P, _P, _P, _P, _C,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

enum {
	TOKEN_ERR_SUCCESS = 0,
	TOKEN_ERR_EOL,
	TOKEN_ERR_UNMATCHED_DQUOTE,
	TOKEN_ERR_UNMATCHED_SQUOTE
};

/*
 * get a token from the string *s, null terminate the token in *s, update *s
 * and *err, and return the token
 * also handle escaping (\) and quoting (" and ') and return an error
 * if there are unmatched quotes
 */
static char *gettoken(char **s, int *err)
{
	int quote = '\0';
	int escape = 0;
	char *c, *token;
	char *out;
	c = *s;
	if (*c == '\0') {
		/* no more tokens */
eol:
		*err = TOKEN_ERR_EOL;
		return NULL;
	}
	while (*c && isspace(*c))
		++c;
	if (*c == '\0') {
		/* no token found */
		goto eol;
	}
	token = c;
	out = c;
	while (*c && (escape || quote || !isspace(*c))) {
		if (escape) {
			/* copy this character literally */
			*out++ = *c;
			escape = 0;
		} else if (*c == '\'' || *c == '"') {
			if (!quote) {
				quote = *c;
			} else if (quote == *c) {
				quote = '\0';
			} else {
				*out++ = *c;
			}
		} else {
			if (*c == '\\' && !quote) escape = 1;
			else *out++ = *c;
		}
		++c;
	}
	if (quote) {
		if (quote == '\'')
			*err = TOKEN_ERR_UNMATCHED_SQUOTE;
		else if (quote == '"')
			*err = TOKEN_ERR_UNMATCHED_DQUOTE;
		return NULL;
	}
	if (*c) {
		*c++ = '\0';
	}
	*out = '\0';
	*s = c;
	return token;
}

void sh_empty_sig_handler(int x)
{
}

#define MAXARGC (BUFSIZE/2+1)
int sh_main(int argc, char **argv, char **envp)
{
	struct utsname utsname;
	char *username = "root";
	char *hostname;
	char *buf;
	ssize_t n;
	char *bp;
	ssize_t len = 0;
	int neof = 0;
	/*
	 * ignoreeof should be initialized with the IGNOREEOF
	 * environment variable
	 */
	int ignoreeof = 0;
	int err;
	int uid;
	struct itimerval it = {
		{ 0, 0 },
		{ 0, 0 }
	};
	int aargc;
	char **aargv;
	int laststatus = 0;
	
	struct sigaction sa;
	sa.sa_flags = 0;

	/* handle SIGINT and SIGQUIT with an empty handler */
	sa.sa_handler = sh_empty_sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	/* ignore some other kill signals */
	sa.sa_handler = SIG_IGN;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGTTIN, &sa, NULL);
	sigaction(SIGTTOU, &sa, NULL);

	/* test continual interruptions to syscalls */
	sa.sa_handler = sh_empty_sig_handler;
	sa.sa_flags = SA_RESTART;
	struct sigaction oldsa;
	sigaction(SIGALRM, &sa, &oldsa);
	//sigaction(SIGALRM, NULL, &oldsa);
	//printf("current sa_flags=%04x\n", oldsa.sa_flags);
	setitimer(ITIMER_REAL, &it, NULL);

	fprintf(stderr, "stupid shell v0.2\n");
	
	/*
	 * Notice that we do not explicitly free these buffers. Punix frees all
	 * user memory allocations for the process upon exit. Nice.
	 */
	buf = malloc(BUFSIZE);
	aargv = malloc(sizeof(char *)*(BUFSIZE/2+1));
	if (!buf || !aargv) {
		fprintf(stderr, "sh: fatal: can't allocate buffers!\n");
		return 1;
	}
	
	uid = getuid();
	err = uname(&utsname);
	if (!err) {
		hostname = utsname.nodename;
	} else {
		hostname = "localhost";
	}
	
	bp = buf;
	for (;;) {
		//setitimer(ITIMER_REAL, &it, NULL);
		if (len == 0) {
			fprintf(stderr, "%s@%s:%s%c ", username, hostname, "~",
			        uid ? '$' : '#');
		}
		n = read(0, bp, BUFSIZE - len);
		if (n < 0) {
			if (errno == EINTR) {
				// clear the input buffer
				bp = buf;
				len = 0;
				continue;
			} else {
				fprintf(stderr, "exiting because errno=%d\n", errno);
				break;
			}
		}
		len += n;
		bp += n;
		if (len == 0) {
			++neof;
			if (neof > ignoreeof) {
				fprintf(stderr, "exit\n");
				break;
			}
			fprintf(stderr, "Use \"exit\" to leave the shell.\n");
			continue;
		}
		// TODO: if input is larger than the buffer (ie, no newline in
		// buf), report an error, gobble up input until a newline is
		// reached, and then resume taking commands as normal
		// OR expand the buffer dynamically
		if (len != BUFSIZE && buf[len-1] != '\n')
			continue;
		
		char *cmd;
		char *s;
		char *token;
		int err = 0;
		
		buf[len-1] = '\0';
		s = buf;
		aargc = 0;
		while ((token = gettoken(&s, &err)) != NULL) {
			if (aargc >= MAXARGC) {
				err = -1; /* XXX */
				break;
			}
			aargv[aargc++] = token;
		}
		if (err && err != TOKEN_ERR_EOL) {
			const char *errstr = "too many arguments";
			switch (err) {
			case TOKEN_ERR_UNMATCHED_DQUOTE:
				errstr = "unmatched double quote";
				break;
			case TOKEN_ERR_UNMATCHED_SQUOTE:
				errstr = "unmatched single quote";
				break;
			}
			fprintf(stderr, "sh: error: %s\n", errstr);
			goto eol;
		}
		aargv[aargc] = NULL;
		
		if (aargc == 0) goto nextline;
		cmd = aargv[0];
		if (!strcmp(cmd, "exit")) {
			break;
		} else if (!strcmp(cmd, "status")) {
			printf("%d\n", laststatus);
			laststatus = 0;
		} else if (!strcmp(cmd, "help")) {
			showhelp(1);
			laststatus = 0;
		} else {
			laststatus = run(cmd, aargc, aargv, envp);
		}
eol:
		neof = 0;
nextline:
		bp = buf;
		len = 0;
	}
	return laststatus;
}

static struct applet applets[] = {
	{ "tests", NULL },
	{ "top", NULL },
	{ "cat", NULL },
	{ "echo", echo_main },
	{ "true", true_main },
	{ "false", false_main },
	{ "clear", clear_main },
	{ "uname", uname_main },
	{ "env", env_main },
	{ "id", id_main },
	{ "pause", pause_main },
	{ "batt", batt_main },
	{ "date", date_main },
	{ "adjtime", adjtime_main },
	{ "malloc", malloc_main },
	{ "pid", pid_main },
	{ "pgrp", pgrp_main },
	{ "poweroff", poweroff_main },
	{ "times", times_main },
	{ "sysctltest", sysctltest_main },
	{ "ps", ps_main },
	{ "bt", bt_main },
	{ "crash", crash_main },
	{ "mul", mul_main },
	{ "div", div_main },
	{ "kill", kill_main },
	{ "time", NULL },
	{ "exit", NULL },
	{ "status", NULL },
	{ "help", NULL },

	{ NULL, NULL }
};

static int run_applet(const char *cmd, int argc, char **argv, char **envp)
{
	struct applet *ap;
	for (ap = &applets[0]; ap->name; ++ap) {
		if (!strcmp(cmd, ap->name)) {
			if (!ap->main) break;
			return ap->main(argc, argv, envp);
		}
	}
	return -1;
}

static int run(const char *cmd, int argc, char **argv, char **envp)
{
	int status;
	int pid;
	char *s;
	status = run_applet(cmd, argc, argv, envp);
	if (status >= 0) return status;
	
	/*
	 * here we would vfork and try to execve(2) the command
	 * in the child (in a real shell, that is)
	 */
	pid = vfork();
	if (pid < 0) {
		perror("sh: cannot vfork");
		return 127;
	} else if (pid == 0) {
		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sa.sa_flags = 0;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sa.sa_handler = SIG_IGN;
		sigaction(SIGTSTP, &sa, NULL);
		sigaction(SIGTTIN, &sa, NULL);
		sigaction(SIGTTOU, &sa, NULL);

		execve(cmd, argv, envp);
		switch (errno) {
		case ENOENT:
			s = "command not found";
			break;
		default:
			s = strerror(errno);
			break;
		}
		fprintf(stderr, "sh: %s: %s\n", cmd, s);
		_exit(127);
	}
	
	/* parent process. we chill here until the child dies */
	for (;;) {
		pid = wait(&status);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			else
				break;
		}
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			break;
		} else if (WIFSIGNALED(status)) {
			status = WTERMSIG(status);
			if (status != SIGINT)
				fprintf(stderr, "terminated with signal %d\n", status);
			status += 128;
			break;
		} else if (WIFSTOPPED(status)) {
			status = WSTOPSIG(status);
			fprintf(stderr, "stopped with signal %d\n", status);
			status += 128;
			break;
		} else {
			fprintf(stderr, "sh: unknown status %d\n", status);
			break;
		}
	}
	
	return status;
}

int bittybox_main(int argc, char **argv, char **envp)
{
	int n;
	if (!strcmp(argv[0], "bittybox")) {
		if (argc < 2) {
			showhelp(0);
			return 0;
		}
		++argv;
		--argc;
	}
	n = run_applet(argv[0], argc, argv, envp);
	if (n < 0) {
		fprintf(stderr, "bittybox: unknown applet \"%s\"\n", argv[0]);
		showhelp(0);
	}
	return n;
}

void sleep_sigalrm(int sig) {}

unsigned sleep(unsigned seconds)
{
	struct sigaction save;
	struct sigaction sa;
	sa.sa_handler = sleep_sigalrm;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, &save);
	alarm(seconds);
	pause();
	sigaction(SIGALRM, &save, NULL);
	return 0;
}

#define GETTYBUFSIZE 42

int getty_main(int argc, char *argv[], char *envp[])
{
	int fd;
	char line[GETTYBUFSIZE];
	char *bp;
	size_t count;
	ssize_t n;
	struct utsname uts;
	const char *dev = "/dev/vt";

	// Usage: getty [devname]
	// devname defaults to /dev/vt
	if (argc == 2) dev = argv[1];

	//sleep(5);
	fd = open(dev, O_RDWR); /* fd 0 */
	if (fd < 0) return -1;
	dup(fd); /* fd 1 */
	dup(fd); /* fd 2 */

	uname(&uts);

prompt:
	bp = line;
	count = GETTYBUFSIZE;
	fprintf(stderr, "\n%s login: ", uts.nodename);
	for (;;) {
		if (count == 0) break;
		n = read(0, bp, count);
		if (n < 0) break;
		if (n == 0) return 1;
		count -= n;
		bp += n;
		if (bp[-1] != '\n') {
			continue;
		}
		bp[-1] = '\0';
		if (strlen(line) == 0)
			goto prompt;
		argv[0] = "login";
		argv[1] = line;
		argv[2] = NULL;
		execve(argv[0], argv, envp);
		fprintf(stderr, "getty: could not execute login!\n");
		break;
	}
	fprintf(stderr, "getty: fail!\n");
	return 1;
}

#define LOGINBUFSIZE 42
int login_main(int argc, char *argv[], char *envp[])
{
	char line[LOGINBUFSIZE];
	char *bp;
	size_t count;
	ssize_t n;
	static const char password[] = "";
	struct sigaction sa;
	char *rootenv[] = {
		"USER=root",
		"LOGNAME=root",
		"SHELL=stupidsh",
		"HOME=/",
		NULL
	};

	count = LOGINBUFSIZE;
	bp = line;
	fprintf(stderr, "password: ");
	for (;;) {
		if (count == 0) goto badpass;
		n = read(0, bp, count);
		if (n < 0) return 1;
		if (n == 0) goto badpass;
		if (bp[n-1] == '\n') break;
		bp += n;
		count -= n;
	}
	
	bp[n-1] = '\0';
	if (!strcmp(argv[1], "root") &&
	    !strcmp(line, password)) {
		argv[0] = "sh";
		argv[1] = NULL;
		execve(argv[0], argv, rootenv);
		return 1;
	}
badpass:
	/* pause for a couple seconds */
	sa.sa_handler = sigalrm;
	sigaction(SIGALRM, &sa, NULL);
	alarm(2);
	pause();
	fprintf(stderr, "bad login!\n");
	return 1;
}

static int spawn_getty(const char *dev)
{
	char *argv[3];
	char *envp[1] = { NULL };
	int pid = vfork();
	if (pid == 0) {
		argv[0] = "getty";
		argv[1] = (char *)dev;
		argv[2] = NULL;
		execve(argv[0], argv, envp);
		_exit(127);
	}
	return pid;
}

int init_main(int argc, char *argv[], char *envp[])
{
	int linkpid = 0, vtpid = -1;
	struct utsname uts;
	
	if (getpid() != 1) {
		/*
		 * We're not the actual init process. We just look like it.
		 * A real init process would tell process 1 to change the
		 * current runlevel or start/stop system process, for example.
		 */
		return 0;
	}

	uname(&uts);
	if (!strcmp(uts.nodename, "server"))
		linkpid = -1;

spawn:
	if (vtpid < 0)
		vtpid = spawn_getty("/dev/vt");
	if (linkpid < 0)
		linkpid = spawn_getty("/dev/link");
	
	/* sit here and reap zombie processes */
	/* also re-spawn getty processes */
	for (;;) {
		int pid;
		int status;
		pid = wait(&status);
		if (pid < 0) {
			if (errno == ECHILD) {
				goto spawn;
			} else {
			}
		} else {
			if (pid == vtpid) vtpid = -1;
			if (pid == linkpid) linkpid = -1;
			goto spawn;
		}
	}
}

static FILE *openstdstream(int fd, const char *mode, int buftype)
{
	FILE *f = fdopen(fd, "r+");
	if (f) setvbuf(f, NULL, buftype, 0);
	return f;
}

void crt()
{
	stdin  = openstdstream(0, "r", _IOLBF);
	stdout = openstdstream(1, "w", _IOLBF);
	stderr = openstdstream(2, "w", _IONBF);
}
