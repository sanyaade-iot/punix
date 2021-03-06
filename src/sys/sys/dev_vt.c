/*
 * dev_vt.c, virtual terminal for Punix
 * Copyright 2007, 2008 Christopher Williams.
 * 
 * This is my first attempt at a VT100 emulator based on the parser state
 * machine at http://vt100.net/emu/dec_ansi_parser
 */

/* FIXME: use the tty structure in each routine to support multiple vt's */
/* TODO: add more escape codes, starting with DECSTBM (Set Top and Bottom
 * Margins). */

#include <sys/types.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#include "punix.h"
#include "cell.h"
#include "queue.h"
#include "tty.h"
#include "proc.h"
#include "glyph.h"
#include "globals.h"

#define NVT 1

#define WINWIDTH  NUMCELLCOLS
#define WINHEIGHT NUMCELLROWS

#define MARGINTOP 0
#define MARGINBOTTOM (WINHEIGHT-1)
#define MARGINLEFT 0
#define MARGINRIGHT (WINWIDTH-1)

#define MAXPARAMVAL 16383

#define NUMPARAMS 16
#define NUMINTCHARS 2
#define UNDEFPARAM (-1) /* undefined parameter value */


#if 0
static const struct tty vt[NVT];

static int xon;

static int privflag;
static int nullop;

static char intchars[NUMINTCHARS+1];
static char *intcharp;

static unsigned params[NUMPARAMS];
static int numparams;

static int cursorvisible;

static int tabstops[WINWIDTH];
static struct pos {
	int row, column;
} pos;

struct attrib {
	int bold       : 1;
	int underscore : 1;
	int blink      : 1;
	int reverse    : 1;
};

static struct attrib currattrib;

struct cell {
	struct attrib attrib;
	int c;
};

struct row {
	/*int width;*/
	struct cell cells[WINWIDTH];
};

/* FIXME: make use of the screen array */
static struct row screen[WINHEIGHT];

#endif

#define GR_NORMAL    0
#define GR_BOLD      1
#define GR_UNDERLINE 2
#define GR_BLINK     4
#define GR_INVERSE   8

#define STGROUND   0 /* ground */
#define STESCAPE   1 /* escape */
#define STESCINT   2 /* escape intermediate */
#define STCSIENT   3 /* csi entry */
#define STCSIPRM   4 /* csi parameter */
#define STCSIINT   5 /* csi intermediate */
#define STCSIIGN   6 /* csi ignore */
#define STDCSENT   7 /* dcs entry */
#define STDCSPRM   8 /* dcs parameter */
#define STDCSINT   9 /* dcs intermediate */
#define STDCSPAS  10 /* dcs passthrough */
#define STDCSIGN  11 /* dcs ignore */
#define STOSCSTR  12 /* osc string */
#define STSOSSTR  13 /* sos/pm/apc string */
#define STLAST    13

struct state {
	void (*entry)(struct tty *);
	void (*event)(int ch, struct tty *);
	void (*exit)(struct tty *);
};

#if 0
static struct state *vtstate;
#endif
static const struct state states[];

/* Transition to a new state */
static void transition(int ch, int newstate,
 void (*trans)(int ch, struct tty *), struct tty *tp)
{
	if (STLAST < (unsigned)newstate) {
		warn("vt: bad state", newstate);
		return;
	}
	if (G.vt.vtstate && G.vt.vtstate->exit)
		G.vt.vtstate->exit(tp);
	if (trans)
		trans(ch, tp);
	
	G.vt.vtstate = &states[newstate];
	if (G.vt.vtstate->entry)
		G.vt.vtstate->entry(tp);
}

#define GLYPHSET_UPPER 0
#define GLYPHSET_UK    1
#define GLYPHSET_US    2
#define GLYPHSET_SG    3
#define GLYPHSET_ALTSG 4

static const struct glyphset glyphsets[] = {
#ifdef SMALLGLYPHS
#include "glyphsets/small-upper.inc"
#include "glyphsets/uk.inc"
#include "glyphsets/small-us.inc"
#include "glyphsets/sg.inc"
/* alt char ROM standard chars here */
/* alt char ROM special graphics (technical character set) */
#include "glyphsets/tcs.inc" 
#else
#include "glyphsets/upper.inc"
#include "glyphsets/uk.inc"
#include "glyphsets/us.inc"
#include "glyphsets/sg.inc"
/* alt char ROM standard chars here */
/* alt char ROM special graphics (technical character set) */
#include "glyphsets/tcs.inc" 
#endif
};

#define SPACEGLYPH (glyphsets[GLYPHSET_US].glyphs[' ' - 0x20])

static void invertcursor(struct tty *tp)
{
	(void)tp;
	xorcursor(G.vt.pos.row,
	          G.vt.pos.column > MARGINRIGHT ? MARGINRIGHT : G.vt.pos.column,
	          G.lcd.grayplanes[0] ? : LCD_MEM);
}

/* move screen contents up */
extern void scrolldown(int n, void *lcd);
/* move screen contents down */
extern void scrollup(int n, void *lcd);

#if 1
static void cursor(struct tty *tp)
{
	if (G.vt.cursorvisible)
		invertcursor(tp);
}
#else
#define cursor(tp) do { if (cursorvisible) invertcursor(); } while (0)
#endif

static void drawgl(struct glyph *glyph, int row, int col);

/* possibly scroll up or down so the cursor is visible again */
static void scroll(struct tty *tp)
{
	int rows;
	if (G.vt.pos.row < 0) {
		rows = -G.vt.pos.row;
		scrollup(rows, G.lcd.grayplanes[0] ? : LCD_MEM);
		if (G.lcd.grayplanes[1])
			scrollup(rows, G.lcd.grayplanes[1]);
		G.vt.pos.row = 0;
	} else if (G.vt.pos.row > MARGINBOTTOM) {
		rows = G.vt.pos.row - MARGINBOTTOM;
		scrolldown(rows, G.lcd.grayplanes[0] ? : LCD_MEM);
		if (G.lcd.grayplanes[1])
			scrolldown(rows, G.lcd.grayplanes[1]);
		G.vt.pos.row = MARGINBOTTOM;
	}
}

#define istabstop(n) (G.vt.tabstops[(n)/8] & (1 << ((n)%8)))
#define settabstop(n) (G.vt.tabstops[(n)/8] |= 1 << ((n)%8))
#define cleartabstop(n) (G.vt.tabstops[(n)/8] &= ~(1 << ((n)%8)))
#define cleartabstops() memset(G.vt.tabstops, 0, sizeof(G.vt.tabstops))

static void cmd_ind(struct tty *tp)
{
	++G.vt.pos.row;
	scroll(tp);
}

static void cmd_nel(struct tty *tp)
{
	G.vt.pos.column = 0;
	++G.vt.pos.row;
	scroll(tp);
}

static void cmd_hts(struct tty *tp)
{
	settabstop(G.vt.pos.column);
}

static void cmd_ri(struct tty *tp)
{
	--G.vt.pos.row;
	scroll(tp);
}

// invoke charset G[from] to G[to]
// to = 0..1 (L, R)
// from = 0..3
static void invokecharset(int to, int from)
{
	memmove(&G.vt.activecharset[32 + 128*to], G.vt.designatedcharsets[from],
		96*sizeof(struct glyph));
	G.vt.activecharsets[to] = from;
}

// to = 0..3
// from = any from the glyphset repertory
static void designatecharset(int to, int set)
{
	int i;
	G.vt.designatedcharsets[to] = &glyphsets[set];
	for (i = 0; i < 2; ++i) {
		if (G.vt.activecharsets[i] == to)
			invokecharset(i, to);
	}
}

static void reset(struct tty *tp)
{
	int i;
	
	G.vt.xon = 1;
	memset((void *)LCD_MEM, 0, 6*NUMCELLROWS*LCD_INCY);
#if 0
	memset(G.vt.screen, 0, sizeof(G.vt.screen));
#endif
	cleartabstops();
	for (i = 0; i < WINWIDTH; i += 8)
		settabstop(i);
	
	G.vt.pos.row = 0;
	G.vt.pos.column = 0;

	G.vt.activecharsets[0] = 0;
	G.vt.activecharsets[1] = 2;
	designatecharset(0, GLYPHSET_US);
	designatecharset(1, GLYPHSET_SG);
	designatecharset(2, GLYPHSET_UPPER);
	designatecharset(3, GLYPHSET_SG); // XXX
	G.vt.cursorvisible = 1;
	G.vt.gr = 0;
	
	G.vt.vtstate = NULL;
	transition(0, STGROUND, NULL, tp);
}

/* clear unset parameters up to parameter 'n' */
static void defaultparams(int n, struct tty *tp)
{
	int i;
	
	if (n > NUMPARAMS)
		n = NUMPARAMS;
	
	for (i = G.vt.numparams; i < n; ++i)
		G.vt.params[i] = UNDEFPARAM;
	
	G.vt.numparams = n + 1;
}

/******************************************************************************
 * Actions
 * 
 * These are implemented according to the descriptions given in the web page
 * referenced at the top of this file and to observations of behaviour of "real"
 * terminal emulators like xterm and rxvt.
 ******************************************************************************/

/* draw the glyph with the current graphic rendition */
static void drawgl(struct glyph *glyph, int row, int col)
{
	if (G.vt.gr & GR_INVERSE)
		drawglyphinv(glyph, row, col, G.lcd.grayplanes[0] ? : LCD_MEM);
	else
		drawglyph(glyph, row, col, G.lcd.grayplanes[0] ? : LCD_MEM);
}

static void print(int ch, struct tty *tp)
{
	/* Display a glyph according to the character set mappings and shift
	 * states in effect. */
	
	/* NB: This defers wrapping to the next line until the cursor is past
	 * the right margin. This means that a newline character will wrap
	 * correctly and will not create a blank line if it arrives after the
	 * character in the right column is printed.
	 * 
	 * Useful information: xterm and rxvt keep the cursor in the right
	 * margin and set a flag when the end of line has been reached. This
	 * means that cursor movement escape codes reset the "end of line" flag,
	 * and movement is relative to the right margin. On the other hand,
	 * gnome-terminal and konsole move the cursor to one column beyond the
	 * right margin. This means that only the "print" routine (such as the
	 * one we're in now) needs to deal with this wrap-around case, and also
	 * movement is relative to the column beyond the right margin.
	 * 
	 * I chose the latter behaviour. However, the cursor is still drawn in
	 * the right margin, even if it's outside the margin (see the
	 * "invertcursor" routine).
	 */
	if (ch == 0x7f) return;
	
	if (G.vt.pos.column > MARGINRIGHT) {
		++G.vt.pos.row;
		G.vt.pos.column = 0;
		scroll(tp);
	}
	
	/* draw the glyph */
	drawgl(&G.vt.activecharset[ch], G.vt.pos.row, G.vt.pos.column);
	
	++G.vt.pos.column;
}

static void tab(int n)
{
	while (n-- > 0) {
		while (G.vt.pos.column < MARGINRIGHT) {
			++G.vt.pos.column;
			if (istabstop(G.vt.pos.column)) break;
		}
	}
}

static void execute(int ch, struct tty *tp)
{
	/* Execute the C0 or C1 control function. */
	switch (ch) {
	case 0x05: /* ENQ */
		/* transmit answerback message (?) */
		break;
	case 0x07: /* BEL */
		bell(tp);
		break;
	case 0x08: /* BS */
		if (G.vt.pos.column > 0)
			--G.vt.pos.column;
		else {
			G.vt.pos.column = MARGINRIGHT;
			--G.vt.pos.row;
			scroll(tp);
		}
		break;
	case 0x09: /* HT */
		/* move the cursor to the next tab stop or the right margin */
		tab(1);
		break;
	case 0x0a: /* LF */
	case 0x0b: /* VT */
	case 0x0c: /* FF */
		/* new line operation */
		++G.vt.pos.row;
		scroll(tp);
		/* fall through */
	case 0x0d: /* CR */
		/* move cursor to the left margin on the current line */
		G.vt.pos.column = 0;
		break;
	case 0x0e: /* SO */
		/* invoke G1 character set, as designated by SCS control
		 * sequence */
		/* Invoke G1 into GL */
		invokecharset(0, 1);
		break;
	case 0x0f: /* SI */
		/* select G0 character set, as selected by ESC ( sequence */
		/* Invoke G0 into GL */
		invokecharset(0, 0);
		break;
	case 0x11: /* XON */
		/* resume transmission */
		G.vt.xon = 1;
		break;
	case 0x13: /* XOFF */
		/* stop transmitting all codes except XOFF and XON */
		G.vt.xon = 0;
		break;
	/* C1 control codes */
	case 'H'+0x40:
		/* HTS - Horizontal Tabulation Set */
		cmd_hts(tp);
		break;
	case 'D'+0x40:
		/* IND - Index */
		cmd_ind(tp);
		break;
	case 'M'+0x40:
		/* RI - Reverse Index */
		cmd_ri(tp);
		break;
	case 'E'+0x40:
		/* NEL - Next Line */
		cmd_nel(tp);
		break;
	}
}

static void clear(struct tty *tp)
{
	/* Clear the current private flag, intermediate characters, final
	 * character, and parameters. */
	
	G.vt.privflag = 0;
	G.vt.nullop = 0;
	
	G.vt.intcharp = &G.vt.intchars[0];
	G.vt.intchars[0] = '\0';
	
	G.vt.numparams = 0;
	G.vt.params[0] = UNDEFPARAM;
}

static void collect(int ch, struct tty *tp)
{
	/* Store the private marker or intermediate character for later use in
	 * selecting a control function to be executed when a final character
	 * arrives. */
	
	if (G.vt.intcharp >= &G.vt.intchars[NUMINTCHARS]) {
		G.vt.nullop = 1; /* flag this so the dispatch turns into a no-op */
		return;
	}
	
	*G.vt.intcharp++ = ch;
	*G.vt.intcharp = '\0';
}

static void param(int ch, struct tty *tp)
{
	/* Collect the characters of a parameter string for a control sequence
	 * or device control sequence and build a list of parameters. The
	 * characters processed by this action are the digits 0-9 and the
	 * semicolon. The semicolon separates parameters. A maximum of 16
	 * parameters need be stored. If more than 16 parameters arrive, all the
	 * extra parameters are silently ignored. */
	
	int v;
	
	if (G.vt.numparams > NUMPARAMS)
		return;
	if (G.vt.numparams == 0) {
		++G.vt.numparams;
		G.vt.params[0] = UNDEFPARAM;
	}

	if (ch == ';') {
		if (G.vt.numparams < NUMPARAMS)
			G.vt.params[G.vt.numparams++] = UNDEFPARAM;
	} else {
		v = G.vt.params[G.vt.numparams-1];
		if (v < 0) v = 0;
		v = v * 10 + (ch - '0');
		if (v > MAXPARAMVAL)
			v = MAXPARAMVAL;
		G.vt.params[G.vt.numparams-1] = v;
	}
}

static void esc_dispatch(int ch, struct tty *tp)
{
	/* Determine the control function to be executed from the intermediate
	 * character(s) and final character, and execute it. The intermediate
	 * characters are available because collect() stored them as they
	 * arrived. */
	
	int s, t;

	if (G.vt.nullop)
		return;
	
	switch (ch) {
#if 0
	case '8':
		/* DECALN # (DEC Private) */
		/* DECRC (DEC Private) */
		break;
	case '3':
		/* DECDHL # (DEC Private) */
		/* top half */
		break;
	case '4':
		/* DECDHL # (DEC Private) */
		/* bottom half */
		break;
	case '6':
		/* DECDWL # (DEC Private) */
		break;
	case 'Z':
		/* DECID (DEC Private) */
		break;
	case '=':
		/* DECKPAM (DEC Private) */
		break;
	case '>':
		/* DECKPNM (DEC Private) */
		break;
	case '7':
		/* DECSC (DEC Private) */
		break;
	case '5':
		/* DECSWL # (DEC Private) */
		break;
#endif
	case 'H':
		/* HTS - Horizontal Tabulation Set */
		cmd_hts(tp);
		break;
	case 'D':
		/* IND - Index */
		cmd_ind(tp);
		break;
	case 'M':
		/* RI - Reverse Index */
		cmd_ri(tp);
		break;
	case 'E':
		/* NEL - Next Line */
		cmd_nel(tp);
		break;
	case 'c':
		/* RIS - Reset to Initial State */
		reset(tp);
		break;
	case 'A': t = GLYPHSET_UK; goto set;
	case 'B': t = GLYPHSET_US; goto set;
	case '0': t = GLYPHSET_SG; goto set;
	case '1': t = GLYPHSET_US; goto set; // XXX
	case '2': t = GLYPHSET_ALTSG; goto set;
set:
		/* SCS - Select Character Set */
		s = -1;
		switch (G.vt.intchars[0]) {
		case '+': ++s;  // 3
		case '*': ++s;  // 2
		case ')': ++s;  // 1
		case '(': ++s;  // 0
		}
		if (s != -1) {
			designatecharset(s, t);
		}
		break;
	}
}

/* clear rows between top and bottom, inclusive */
static void clearrows(int top, int bottom)
{
	int n = bottom - top + 1;
	if (n <= 0) return;
	memset((G.lcd.grayplanes[0] ? : LCD_MEM)+180*top, -!!(G.vt.gr & GR_INVERSE), 180*n);
	if (G.lcd.grayplanes[1])
		memset(G.lcd.grayplanes[1]+180*top, -!!(G.vt.gr & GR_INVERSE), 180*n);
}

static void csi_dispatch(int ch, struct tty *tp)
{
	/* Determine the control function to be executed from private marker,
	 * intermediate character(s) and final character, and execute it,
	 * passing in the parameter list. The private marker and intermediate
	 * characters are available because collect() stored them as they
	 * arrived. */
	
	int n, r, c;
	int i;
	
	if (G.vt.nullop)
		return;
	
	if (G.vt.numparams > NUMPARAMS) G.vt.numparams = NUMPARAMS;
	
	switch (ch) {
	case 'A':
		/* CUU Pn - Cursor Up */
		if (G.vt.pos.row == MARGINTOP) break;
		n = G.vt.params[0];
		if (n <= 0)
			n = 1;
		
		G.vt.pos.row -= n;
		if (G.vt.pos.row < 0)
			G.vt.pos.row = 0;
		break;
	case 'B':
		/* CUD Pn - Cursor Down */
		if (G.vt.pos.row == MARGINBOTTOM) break;
		n = G.vt.params[0];
		if (n <= 0)
			n = 1;
		
		G.vt.pos.row += n;
		if (G.vt.pos.row > MARGINBOTTOM)
			G.vt.pos.row = MARGINBOTTOM;
		break;
	case 'C':
		/* CUF Pn - Cursor Forward */
		n = G.vt.params[0];
		if (n <= 0)
			n = 1;
		
		G.vt.pos.column += n;
		if (G.vt.pos.column > MARGINRIGHT)
			G.vt.pos.column = MARGINRIGHT;
		break;
	case 'D':
		/* CUB Pn - Cursor Backward */
		n = G.vt.params[0];
		if (n <= 0)
			n = 1;
		
		G.vt.pos.column -= n;
		if (G.vt.pos.column < 0)
			G.vt.pos.column = 0;
		break;
	case 'E':
		/* FIXME: CNL - Cursor Next Line */
		break;
	case 'F':
		/* FIXME: CPL - Cursor Preceding Line */
		break;
	case 'G':
		/* CHA - Cursor Character Absolute */
		defaultparams(1, tp);
		c = G.vt.params[0];
		
		G.vt.pos.column = c - 1;
		if (G.vt.pos.column < 0)
			G.vt.pos.column = 0;
		else if (G.vt.pos.column > MARGINRIGHT)
			G.vt.pos.column = MARGINRIGHT;
		break;
	case 'H':
		/* CUP Pr Pc - Cursor Position */
	case 'f':
		/* HVP Pn Pn - Horizontal and Vertical Position */
		defaultparams(2, tp);
		r = G.vt.params[0];
		c = G.vt.params[1];
		
		G.vt.pos.row = r - 1;
		if (G.vt.pos.row < 0)
			G.vt.pos.row = 0;
		else if (G.vt.pos.row > MARGINBOTTOM)
			G.vt.pos.row = MARGINBOTTOM;
		
		G.vt.pos.column = c - 1;
		if (G.vt.pos.column < 0)
			G.vt.pos.column = 0;
		else if (G.vt.pos.column > MARGINRIGHT)
			G.vt.pos.column = MARGINRIGHT;
		break;
	case 'I':
		/* CHT - Cursor Forward Tabulation */
		defaultparams(1, tp);
		c = G.vt.params[0];
		if (c == -1) c = 1;
		tab(c);
		break;
	case 'M':
		/* FIXME: DL - Delete Line */
		break;
	case 'c':
		/* DA Pn - Device Attributes */
		/* FIXME: send a response to the host */
		break;
	case 'q':
		/* DECLL Ps (DEC Private) */
		break;
	case 'x':
		/* DECREQTPARM sol */
		/* DECREPTPARM sol par nbits xspeed rspeed clkmul flags */
		/* FIXME */
		break;
#if 0
	case 'r':
		/* DECSTBM Pn Pn (DEC Private) */
		/* FIXME: add static variables margintop and marginbottom,
		 * and use them in scrolling and cursor movement code */
		int t = G.vt.params[0];
		int b = G.vt.params[1];
		if (t < 0 && b < 0) {
			G.vt.margintop = 1;
			G.vt.marginbottom = MARGINBOTTOM;
		} else {
			if (t <= 0) t = 1;
			if (b <= 0) b = 1;
			if (b > MARGINBOTTOM) b = MARGINBOTTOM;
			if (b <= t) break;
			G.vt.margintop = t;
			G.vt.marginbottom = b;
		}
		home();
		break;
#endif
#if 0
	case 'y':
		/* DECTST 2 Ps */
		defaultparams(2, tp);
		if (G.vt.params[0] != 2)
			break;
		
		n = G.vt.params[1];
		
		while (n & 8) /* Repeat Selected Test(s) indefinitely */
			if (n == 0)
				; /* reset the VT100 */
			else if (n & 1)
				; /* Power up self-test */
			else if (n & 2)
				; /* Data Loop Back */
			else if (n & 4)
				; /* EIA modem control test */
		break;
#endif
	case 'n':
		/* DSR Ps */
		/* Device Status Report */
		break;
	case 'J':
		/* ED Ps */
		/* Erase in Display */
		n = G.vt.params[0];
		
		if (n <= 0) {
			/* Erase from the active position to the end of the
			 * screen, inclusive (default) */
			for (c = G.vt.pos.column; c < WINWIDTH; ++c)
				drawgl(&SPACEGLYPH, G.vt.pos.row, c);
			clearrows(G.vt.pos.row + 1, WINHEIGHT-1);
		} else if (n == 1) {
			/* Erase from start of the screen to the active
			 * position, inclusive */
			clearrows(0, G.vt.pos.row - 1);
			for (c = 0; c <= G.vt.pos.column; ++c)
				drawgl(&SPACEGLYPH, G.vt.pos.row, c);
		} else if (n == 2) {
			/* Erase all of the display -- all lines are erased,
			 * changed to single-width, and the cursor does not
			 * move. */
			clearrows(0, WINHEIGHT - 1);
		}
		break;
	case 'K':
		/* EL Ps */
		/* Erase in Line */
		n = G.vt.params[0];
		
		if (n <= 0) {
			/* Erase from the active position to the end of the
			 * line, inclusive (default) */
			for (c = G.vt.pos.column; c < WINWIDTH; ++c)
				drawgl(&SPACEGLYPH, G.vt.pos.row, c);
		} else if (n == 1) {
			/* Erase from the start of the line to the active
			 * position, inclusive */
			for (c = 0; c <= G.vt.pos.column; ++c)
				drawgl(&SPACEGLYPH, G.vt.pos.row, c);
		} else if (n == 2) {
			/* Erase all of the line, inclusive */
			for (c = 0; c < WINWIDTH; ++c)
				drawgl(&SPACEGLYPH, G.vt.pos.row, c);
		}
		break;
	case 'h':
		/* SM Ps ... */
		/* Set Mode */
		
		for (i = 0; i < G.vt.numparams; ++i)
			switch (G.vt.params[i]) {
			case 1:  /* DECCKM */
				break;
			case 2:  /* DECCANM */
				break;
			case 3:  /* DECCOLM */
				break;
			case 4:  /* DECSCLM */
				break;
			case 5:  /* DECSCNM */
				break;
			case 6:  /* DECOM */
				break;
			case 7:  /* DECAWM */
				break;
			case 8:  /* DECARM */
				break;
			case 9:  /* DECINLM */
				break;
			}
		
		break;
	case 'l':
		/* RM Ps ... */
		/* Reset Mode */
		
		for (i = 0; i < G.vt.numparams; ++i)
			switch (G.vt.params[i]) {
			case 1:  /* DECCKM */
				break;
			case 2:  /* DECCANM */
				break;
			case 3:  /* DECCOLM */
				break;
			case 4:  /* DECSCLM */
				break;
			case 5:  /* DECSCNM */
				break;
			case 6:  /* DECOM */
				break;
			case 7:  /* DECAWM */
				break;
			case 8:  /* DECARM */
				break;
			case 9:  /* DECINLM */
				break;
			case 20: /* LNM */
				break;
			}
		
		break;
	case 'm':
		/* SGR Ps ... */
		/* FIXME: Select Graphic Rendition */
		
		if (G.vt.numparams == 0) {
			G.vt.gr = GR_NORMAL;
			break;
		}
		
		for (i = 0; i < G.vt.numparams; ++i) {
			switch (G.vt.params[i]) {
			case 0:
				/* attributes off */
				G.vt.gr = GR_NORMAL;
				break;
			case 1:
				/* bold or increased intensity */
				break;
			case 4:
				/* underscore */
				break;
			case 5:
				/* blink */
				break;
			case 3:
				/* italic (treat it like reverse image) */
				/* fall through */
			case 7:
				/* negative (reverse image) */
				G.vt.gr |= GR_INVERSE;
				break;
			case 23:
				/* italic off */
			case 27:
				/* negative image off */
				G.vt.gr &= ~GR_INVERSE;
				break;
			}
		}
		
		break;
	case 'g':
		/* TBC Ps - Tabulation Clear */
		/* FIXME */
		switch (G.vt.params[0]) {
		case 0:
			/* Clear the horizontal tab stop at the active
			 * position (default) */
			cleartabstop(G.vt.pos.column);
			break;
#if 0
		case 1:
			/* Clear vertical tab stop at active line */
			break;
		case 2:
			/* Clear all horizontal tab stops in active line */
			break;
#endif
		case 3:
			/* Clear all horizontal tab stops */
			cleartabstops();
			break;
#if 0
		case 4:
			/* Clear all vertical tab stops */
			break;
#endif
		}
		
		break;
	default:
		/* error ? */
		;
	}
}

/* NOTE: The following actions will probably be a no-op in this vt. */

static void hook(struct tty *tp)
{
	/* FIXME: Determine the control function from the private marker,
	 * intermediate character(s) and final character, and execute it,
	 * passing in the parameter list. Also select a handler function for the
	 * rest of the characters in the control string. This handler function
	 * will be called by the put() action for every character in the control
	 * string as it arrives.
	 * 
	 * This way of handling device control strings has been selected because
	 * it allows the simple plugging-in of extra parsers as functionality is
	 * added. Support for a fairly simple control string like DECDLD
	 * (Downline Load) could be added into the main parser if soft
	 * characters were required, but the main parser is no place for
	 * complicated protocols like ReGIS. */
}

static void put(int ch, struct tty *tp)
{
	/* FIXME: Pass characters from the data string part of a device control
	 * string to a handler that has previously been selected by the hook()
	 * action. C0 controls are also passed to the handler. */
}

static void unhook(struct tty *tp)
{
	/* FIXME: Call the previously selected handler function with an "end of
	 * data" parameter. This allows the handler to finish neatly. */
}

static void osc_start(struct tty *tp)
{
	/* FIXME: Initialize an external parser (the "OSC Handler") to handle
	 * the characters from the control string. OSC control strings are not
	 * structured in the same way as device control strings, so there is no
	 * choice of parsers. */
}

static void osc_put(int ch, struct tty *tp)
{
	/* FIXME: Pass characters from the control string to the OSC Handler as
	 * they arrive. There is therefore no need to buffer characters until
	 * the end of the control string is recognised. */
}

static void osc_end(struct tty *tp)
{
	/* FIXME: Allow the OSC Handler to finish neatly. */
}

/***************************************
 * State events
 ***************************************/

static void ground_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x7f) {
		int whereami = G.whereami;
		G.whereami = WHEREAMI_PRINT;
		print(ch, tp);
		G.whereami = whereami;
	}
}

static void escape_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x2f)
		transition(c, STESCINT, collect, tp);
	else if (c == 0x50)
		transition(c, STDCSENT, NULL, tp);
	else if (c == 0x5b)
		transition(c, STCSIENT, NULL, tp);
	else if (c == 0x5d)
		transition(c, STOSCSTR, NULL, tp);
	else if (c == 0x58 || c == 0x5e || c == 0x5f)
		transition(c, STSOSSTR, NULL, tp);
	else if (c <= 0x7e)
		transition(c, STGROUND, esc_dispatch, tp);
}

static void escint_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x2f)
		collect(c, tp);
	else if (c <= 0x7e)
		transition(c, STGROUND, esc_dispatch, tp);
}

static void csient_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x2f)
		transition(c, STCSIINT, collect, tp);
	else if (c == 0x3a)
		transition(c, STCSIIGN, NULL, tp);
	else if (c <= 0x3b)
		transition(c, STCSIPRM, param, tp);
	else if (c <= 0x3f)
		transition(c, STCSIPRM, collect, tp);
	else if (c <= 0x7e)
		transition(c, STGROUND, csi_dispatch, tp);
}

static void csiprm_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x2f)
		transition(c, STCSIINT, collect, tp);
	else if (c <= 0x39 || c == 0x3b)
		param(c, tp);
	else if (c <= 0x3f)
		transition(c, STCSIIGN, NULL, tp);
	else if (c <= 0x7e)
		transition(c, STGROUND, csi_dispatch, tp);
}

static void csiint_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (c <= 0x2f)
		collect(c, tp);
	else if (c <= 0x7e)
		transition(c, STGROUND, csi_dispatch, tp);
}

static void csiign_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (ch <= 0x1f)
		execute(ch, tp);
	else if (0x40 <= c && c <= 0x7e)
		transition(c, STGROUND, NULL, tp);
}

static void dcsent_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (0x20 <= c && c <= 0x2f)
		transition(c, STDCSINT, collect, tp);
	else if (c == 0x3a)
		transition(c, STDCSIGN, NULL, tp);
	else if (c <= 0x3b)
		param(c, tp);
	else if (c <= 0x3f)
		transition(c, STDCSPRM, collect, tp);
	else if (c <= 0x7e)
		transition(c, STDCSPAS, NULL, tp);
}

static void dcsprm_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (0x20 <= c && c <= 0x2f)
		transition(c, STDCSINT, collect, tp);
	else if (c <= 0x39 || c == 0x3b)
		param(c, tp);
	else if (c <= 0x3f)
		transition(c, STDCSIGN, NULL, tp);
	else if (c <= 0x7e)
		transition(c, STDCSPAS, NULL, tp);
}

static void dcsint_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (0x20 <= c && c <= 0x2f)
		collect(c, tp);
	else if (c <= 0x3f)
		transition(c, STDCSIGN, NULL, tp);
	else if (c <= 0x7e)
		transition(c, STDCSPAS, NULL, tp);
}

static void dcspas_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (c <= 0x7e)
		put(ch, tp);
}

static void dcsign_event(int ch, struct tty *tp)
{
}

static void oscstr_event(int ch, struct tty *tp)
{
	int c = ch & 0x7f;
	
	if (0x20 <= c && c <= 0x7f)
		osc_put(ch, tp);
}

static void sosstr_event(int ch, struct tty *tp)
{
	if (ch == 0x07)
		transition(ch, STGROUND, NULL, tp);
}

static const struct state states[] = {
	{ NULL, ground_event, NULL }, /* ground */
	{ clear, escape_event, NULL }, /* escape */
	{ NULL, escint_event, NULL }, /* escape intermediate */
	{ NULL, csient_event, NULL }, /* csi entry */
	{ NULL, csiprm_event, NULL }, /* csi parameter */
	{ NULL, csiint_event, NULL }, /* csi intermediate */
	{ NULL, csiign_event, NULL }, /* csi ignore */
	{ clear, dcsent_event, NULL }, /* dcs entry */
	{ NULL, dcsprm_event, NULL }, /* dcs parameter */
	{ NULL, dcsint_event, NULL }, /* dcs intermediate */
	{ hook, dcspas_event, unhook }, /* dcs passthrough */
	{ NULL, dcsign_event, NULL }, /* dcs ignore */
	{ osc_start, oscstr_event, osc_end }, /* osc string */
	{ NULL, sosstr_event, NULL }, /* sos/pm/apc string */
};

/***************************************
 * vt device interface
 ***************************************/

/* one-shot routine on system startup */
void vtinit()
{
	short *horline;
	
#if CALC_HAS_LARGE_SCREEN
	for (horline = (short *)&LCD_MEM[LCD_INCY*6*NUMCELLROWS];
	 horline < (short *)&LCD_MEM[LCD_INCY*6*NUMCELLROWS+LCD_INCY];
	  ++horline)
		*horline = ~0;
#endif
	
	G.vt.vtstate = &states[STGROUND];
	reset(&G.vt.vt[0]); /* XXX dev */
	cursor(&G.vt.vt[0]);
	G.vt.key_repeat_start_delay = (unsigned char)((long)HZ * KEY_REPEAT_DELAY / 1000);
	G.vt.key_repeat_delay = HZ / KEY_REPEAT_RATE;
	G.vt.key_repeat = 1;
	G.vt.key_compose = 0;
	G.vt.compose = 0;
	G.vt.key_mod_sticky = 0;
	G.vt.lock = 0;
	G.vt.scroll_lock = 0;
	G.vt.bell = 0;
	G.cpubusy = 1;
	showstatus();
	qinit(&G.vt.vt[0].t_rawq.q, LOG2TTYQSIZE);
	qinit(&G.vt.vt[0].t_canq.q, LOG2TTYQSIZE);
	//qinit(&G.vt.vt[0].t_outq.q, LOG2TTYQSIZE);
	kbinit();
	kprintf("grayplanes[0]=%p\n", G.lcd.grayplanes[0]);
	kprintf("grayplanes[1]=%p\n", G.lcd.grayplanes[1]);
}

/* FIXME: use the tty structure */
static void dovtoutput(int ch, struct tty *tp)
{
	int x = spl7();
	cursor(tp);
	
	/* process the "anywhere" pseudo-state */
	if (ch == 0x1b)
		transition(ch, STESCAPE, NULL, tp);
	else if (ch == 0x90)
		transition(ch, STDCSENT, NULL, tp);
	else if (ch == 0x98 || ch == 0x9e || ch == 0x9f)
		transition(ch, STSOSSTR, NULL, tp);
	else if (ch == 0x9b)
		transition(ch, STCSIENT, NULL, tp);
	else if (ch == 0x9c)
		transition(ch, STGROUND, NULL, tp);
	else if (ch == 0x9d)
		transition(ch, STOSCSTR, NULL, tp);
	else if (ch == 0x18 || ch == 0x1a ||
	         (0x80 <= ch && ch <= 0x9a))
		transition(ch, STGROUND, execute, tp);
	else
		G.vt.vtstate->event(ch, tp);
	
	cursor(tp);
	splx(x);
}

static void vtoutput(int ch, struct tty *tp)
{
#if 0
	int c;
	int x = spl7();
	if (G.vt.lock) {
		qputc(ch, &tp->t_outq.q);
		splx(x);
		return;
	}
	++G.vt.lock;
	splx(x);

	dovtoutput(ch, tp);
	x = spl7();
	while ((c = qgetc(&tp->t_outq.q)) != -1) {
		splx(x);
		dovtoutput(c, tp);
		x = spl7();
	}
	G.vt.lock = 0;
	splx(x);
#else
	dovtoutput(ch, tp);
#endif
}

/*
 * this version of ttyoutput bypasses the t_outq.q FIFO, handling
 * all terminal output in the same context as the current process.
 */
static void ttyoutput(int ch, struct tty *tp)
{
	int oflag = tp->t_oflag;
	int lflag = tp->t_lflag;
	cc_t *cc = tp->t_cc;
	if (oflag & OPOST) {
		if (ch == '\n') {
			if (oflag & ONLCR)
				ttyoutput('\r', tp);
			else if (oflag & ONLRET)
				ch = '\r';
		}
		if (oflag & OCRNL && ch == '\r')
			ch = '\n';
	}
	vtoutput(ch, tp);
}

/* from 4.4BSD-Lite: */
/*
 * Echo a typed character to the terminal.
 */
static void ttyecho(int c, struct tty *tp)
{
	c &= 0xff;
#if 0
	if (!tp->t_state & TS_CNTTB)
		tp->t_lflag &= ~FLUSHO;
#endif
	if ((!(tp->t_lflag & ECHO) &&
	    (!(tp->t_lflag & ECHONL) || c == '\n')) /* ||
	    (tp->t_lflag & EXTPROC)*/)
		return;
	if ((tp->t_lflag & ECHOCTL) &&
	    ((c <= 0x1f && c != '\n') ||
	    c == 0x7f)) {
		(void)ttyoutput('^', tp);
		if (c == 0177)
			c = '?';
		else
			c += 'A' - 1;
	}
	ttyoutput(c, tp);
}

void ttyrubo(struct tty *tp, int count)
{
	while (count-- > 0) {
		ttyoutput('\b', tp);
		ttyoutput(' ', tp);
		ttyoutput('\b', tp);
	}
}

void ttyrub(int ch, struct tty *tp)
{
	int lflag = tp->t_lflag;
	if (!(lflag & ECHO) /* || ISSET(tp->t_lflag, EXTPROC) */)
		return;

	if ((lflag & ECHOE)) {
		/* FIXME: handle different character classes */
		if ((lflag & ECHOCTL) && ch <= 037)
			ttyrubo(tp, 2);
		else
			ttyrubo(tp, 1);
#if 0
	} else if ((lflag & ECHOPRT)) {
		if (!(tp->t_state & TS_ERASE)) {
			tp->t_state |= TS_ERASE;
			ttyoutput('\\', tp);
		}
		ttyecho(ch, tp);
#endif
        } else
                ttyecho(tp->t_cc[VERASE], tp);
        //--tp->t_rocount;

}

void ttyretype(struct tty *tp)
{
	ttyoutput('\r', tp);
	ttyoutput('\n', tp);
	/* TODO */
}

extern void ttywakeup(struct tty *tp);
/*
 * ttyinput is implemented here because it calls ttyoutput which has an
 * implementation specific to dev_vt
 */
/*
 * TODO: remove redundant code; look at 4.4BSD-Lite for good working
 * (and clean) code
 */
static void ttyinput(int ch, struct tty *tp)
{
	int i;
	int iflag = tp->t_iflag;
	int lflag = tp->t_lflag;
	cc_t *cc = tp->t_cc;
	
	ch &= 0xff;
	
	if (ch == '\r') {
		if (iflag & IGNCR)
			return;
		if (iflag & ICRNL)
			ch = '\n';
	} else if (ch == '\n' && iflag & INLCR)
		ch = '\r';
	if (ch != '\0') {
		if (ch == cc[VSTART]) {
			if (tp->t_state & TTSTOP) {
				tp->t_state &= ~TTSTOP;
				/*ttstart(tp);*/
			}
			return;
		}
		if (ch == cc[VSTOP]) {
			if (!(tp->t_state & TTSTOP)) {
				tp->t_state |= TTSTOP;
				/*cdevsw[MAJOR(tp->t_dev)].d_stop(tp);*/
			}
			return;
		}
		if (ch == cc[VSUSP]) {
			ttyecho(ch, tp);
			if (lflag & ECHO) {
				ttyoutput('\n', tp);
			}
			if (lflag & ISIG) {
				gsignal(tp->t_pgrp, SIGTSTP);
				if (!(lflag & NOFLSH))
					flushtty(tp);
				return;
			}
		}
		if (ch == cc[VINTR] || ch == cc[VQUIT]) {
			ttyecho(ch, tp);
			if (lflag & ECHO) {
				ttyoutput('\n', tp);
			}
			if (lflag & ISIG) {
				gsignal(tp->t_pgrp,
					ch == cc[VQUIT] ? SIGQUIT : SIGINT);
				gsignal(tp->t_pgrp, SIGCONT);
				if (!(lflag & NOFLSH))
					flushtty(tp);
				return;
			}
		}
	}
	if (1 && (tp->t_state & TTSTOP)) {
		tp->t_state &= ~TTSTOP;
		/*ttstart(tp);*/
	}

	if (lflag & ICANON) {
		if (ch == '\0') goto putchar;
		if (ch == cc[VERASE]) {
			if (!qisempty(&tp->t_rawq.q))
				ttyrub(qunputc(&tp->t_rawq.q), tp);
			goto endcase;
		}
		if (ch == cc[VKILL]) {
#if 1
			if ((lflag & ECHOKE) /* && ... */)
				while (!qisempty(&tp->t_rawq.q))
					ttyrub(qunputc(&tp->t_rawq.q), tp);
			else
#endif
			{
				ttyecho(ch, tp);
				if ((lflag & ECHOK) /*|| (lflag & ECHOKE)*/ )
					ttyecho('\n', tp);
				qclear(&tp->t_rawq.q); /* ??? */
				//tp->t_rocount = 0;
			}
			goto endcase;
		}
		if (ch == cc[VWERASE]) {
			/* first erase whitespace */
			while ((ch = qunputc(&tp->t_rawq.q)) == ' ' || ch == '\t')
				ttyrub(ch, tp);
			while (ch != -1 && ch != ' ' && ch != '\t') {
				ttyrub(ch, tp);
				ch = qunputc(&tp->t_rawq.q);
			}
			if (ch != -1)
				qputc(ch, &tp->t_rawq.q);
			goto endcase;
		}
		if (lflag & IEXTEN) {
			if (ch == cc[VREPRINT]) {
				// reprint the line
				ttyretype(tp);
				goto endcase;
			}
#if 0
			if (ch == cc[VSTATUS]) {
				// show info
				// TODO
				goto endcase;
			}
#endif
		}

putchar:
		/*
		 * put the character on the character queue
		 */
		TRACE();
		if (TTBREAKC(ch) ||
		    !(iflag & IMAXBEL) ||
		    (qused(&tp->t_rawq.q) + 1) < qfree(&tp->t_canq.q)) {
		TRACE();
			if (qputc(ch, &tp->t_rawq.q) < 0) {
				flushtty(tp);
				return;
			}
			ttyecho(ch, tp);
		} else {
			ttyoutput('\a', tp);  // bell
		}
		TRACE();
		if (TTBREAKC(ch)) {
			//tp->t_rocount = 0;
			catq(&tp->t_rawq.q, &tp->t_canq.q);
			defer(ttywakeup, tp);
		} /*else if (tp->t_rocount++ == 0)
			tp->t_rocol = tp->t_column;*/
#if 0
		if (tp->t_state & TS_ERASE) {
			/* end of prterase \.../ */
			tp->t_state &= ~TS_ERASE;
			ttyoutput('/', tp);
		}
#endif
		/*i = tp->t_column;*/
#if 1
		if (ch == cc[VEOF] && (lflag & ECHO)) {
			/* Place the cursor over the '^' of the ^D. */
/*
			i = MIN(2, tp->t_column - i);
			while (i-- > 0)
				ttyoutput('\b', tp);
*/
			ttyoutput('\b', tp);
			ttyoutput('\b', tp);
		}
#endif
/*
		if (ch == cc[VEOL] || ch == cc[VEOF]) {
			catq(&tp->t_rawq.q, &tp->t_canq.q);
			defer(ttywakeup, tp);
		}
*/
	} else {
		if (qputc(ch, &tp->t_rawq.q) < 0) {
			flushtty(tp);
			return;
		}
		defer(ttywakeup, tp);
		ttyecho(ch, tp);
	}
endcase: ;
}

/* NB: there is no vtxint routine */

void vtrint(dev_t dev, int ch)
{
#if 1
	ttyinput(ch, &G.vt.vt[MINOR(dev)]);
#else
	ttyoutput(ch, &G.vt.vt[MINOR(dev)]); /* XXX for debugging/testing */
#endif
}

int kputchar(int ch)
{
	struct tty *tp = &G.vt.vt[0]; /* XXX */
	ch &= 0xff;
	vtoutput(ch, tp);
	return ch;
}

int vt_open(struct file *fp, struct inode *ip)
{
	int dev = ip->i_rdev;
	int minor = MINOR(dev);
	struct tty *tp;
	
	if (minor >= NVT) {
		P.p_error = ENXIO;
		return -1;
	}
	tp = &G.vt.vt[minor];
	if (!(tp->t_state & ISOPEN)) {
/*
   sane          same as cread brkint icrnl 
                 imaxbel opost onlcr
                 nl0 cr0 tab0 bs0 vt0 ff0
                 isig icanon iexten echo echoe echok 
                 echoctl echoke, all special
                 characters to their default values.
input	BRKINT|ICRNL  |IMAXBEL
output	OPOST|ONLCR|NL0|CR0|TAB0|BS0|VT0|FF0
control	CREAD
local	ISIG|ICANON|IEXTEN|ECHO|ECHOE|ECHOK  |ECHOCTL|ECHOKE
*/

		tp->t_state = ISOPEN;
		/* should I put these in tty.c? */
		tp->t_iflag = /*BRKINT|*/ ICRNL|IXANY|IMAXBEL;
		tp->t_oflag = OPOST|ONLCR;
		tp->t_cflag = CREAD;
		tp->t_lflag = ISIG|ICANON|IEXTEN|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE;
		ttychars(tp);
	}
	ttyopen(dev, tp);
	return 0;
}

ssize_t tty_read(struct tty *tp, void *buf, size_t count);
ssize_t vt_read(struct file *fp, void *buf, size_t count, off_t *pos)
{
	dev_t dev = fp->f_inode->i_rdev;
	return tty_read(&G.vt.vt[MINOR(dev)], buf, count);
}

size_t vt_write(struct file *fp, void *buf, size_t count, off_t *pos)
{
	size_t n = 0;
	char ch;
	dev_t dev = fp->f_inode->i_rdev;
	struct tty *tp = &G.vt.vt[MINOR(dev)];

#if 0
	/* we do need to be sure the tty device is open, but we'll ignore this
	 * for now */
	if (!(tp->t_state & ISOPEN)) {
		P.p_error = ENXIO; // ???
		return -1;
	}
#endif

	while (count > 0) {
		if (copyin(&ch, buf, 1)) {
			if (n) return n;
			P.p_error = EFAULT;
			return -1;
		}
		ttyoutput(ch, tp);
		++buf;
		++n;
		--count;
	}

	return n;
}

int vt_close(struct file *fp)
{
	return 0;
}

int vt_ioctl(struct file *fp, int request, void *arg)
{
	// TODO: support tty ioctl's
	P.p_error = EINVAL;
	return -1;
}

off_t pipe_lseek(struct file *, off_t, int);
int generic_file_fstat(struct file *fp, struct stat *buf);
const struct fileops vt_fileops = {
	.open = vt_open,
	.close = vt_close,
	.read = vt_read,
	.write = vt_write,
	.lseek = pipe_lseek,
	.ioctl = vt_ioctl,
	.fstat = generic_file_fstat,
};
